# End-to-end test: private DHT + two sidecars + fake LAN world (TCP echo).
# Usage: TUN_HOST=127.0.0.1 python3 -I tests/e2e.py target/debug/fashion-net [real]
import json, os, socket, struct, subprocess, sys, threading, time, shutil
BIN = sys.argv[1]
REAL_DHT = len(sys.argv) > 2 and sys.argv[2] == "real"
root = "/tmp/fn-test"; shutil.rmtree(root, ignore_errors=True); os.makedirs(root)
procs = []
def spawn(args, **kw):
    p = subprocess.Popen(args, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=open(f"{root}/{kw.pop('log')}.log", "w"), text=True, bufsize=1)
    procs.append(p); return p
boot = []
if not REAL_DHT:
    t = spawn([BIN, "--dht-testnet", "6"], log="testnet")
    boot = ["--bootstrap", t.stdout.readline().strip()]
class Side:
    def __init__(s, name):
        extra = [] if REAL_DHT else ["--no-stun"]
        s.p = spawn([BIN, "--data", f"{root}/{name}", "--name", name, "--loopback", *boot, *extra], log=name)
        s.state = None; s.notices = []; s.lock = threading.Lock()
        threading.Thread(target=s.read, daemon=True).start()
    def read(s):
        for line in s.p.stdout:
            ev = json.loads(line)
            with s.lock:
                if ev["ev"] == "state": s.state = ev
                else: s.notices.append(ev)
    def cmd(s, **c): s.p.stdin.write(json.dumps(c) + "\n"); s.p.stdin.flush()
    def wait(s, pred, what, timeout=120):
        end = time.time() + timeout
        while time.time() < end:
            with s.lock:
                try:
                    if s.state and pred(s.state): return s.state
                except Exception: pass
            time.sleep(0.2)
        raise SystemExit(f"TIMEOUT waiting for {what}; state={json.dumps(s.state)[:1500]}")
t0 = time.time()
def log(m): print(f"[{time.time()-t0:6.1f}s] {m}", flush=True)
A, B = Side("Alice"), Side("Bob")
A.wait(lambda s: s["net"]["dht"], "A dht"); B.wait(lambda s: s["net"]["dht"], "B dht"); log("both bootstrapped")
a = A.wait(lambda s: s["me"]["code"], "A code")["me"]; b = B.state["me"]
log(f"Alice code {a['code']}  link {a['link']}")
time.sleep(5)  # let presence publish
B.cmd(cmd="add_friend", code=a["link"])
A.wait(lambda s: len(s["requests"]) == 1, "request at Alice"); log(f"Alice got request: {A.state['requests']}")
A.cmd(cmd="accept_request", key=b["key"])
B.wait(lambda s: s["friends"][0]["conn"] == "direct" and not s["friends"][0]["pending"], "Bob friend accepted"); log(f"Bob sees: {B.state['friends'][0]}")
A.wait(lambda s: s["friends"] and s["friends"][0]["name"] == "Bob", "Alice friend list"); log("Alice sees Bob as friend")
# fake LAN world on the host
srv = socket.socket(); srv.bind(("127.0.0.1", 0)); srv.listen(); wport = srv.getsockname()[1]
def echo():
    while True:
        c, _ = srv.accept()
        def h(c):
            while (d := c.recv(65536)): c.sendall(d)
            c.close()
        threading.Thread(target=h, args=(c,), daemon=True).start()
threading.Thread(target=echo, daemon=True).start()
def announce():
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM); s.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_TTL, 1)
    while True:
        s.sendto(f"[MOTD]Alice - Test World[/MOTD][AD]{wport}[/AD]".encode(), ("224.0.2.60", 4445)); time.sleep(1.5)
threading.Thread(target=announce, daemon=True).start()
A.wait(lambda s: s["hosting"]["world"] and s["hosting"]["world"]["port"] == wport, "Alice detects LAN world"); log(f"Alice detects world on {wport}")
A.cmd(cmd="invite", keys=[b["key"]], invite={"instance": "Test Pack", "mc": "1.21.4", "manifest": {"mods": [{"id": "x", "name": "Sodium", "side": "client"}]}})
inv = B.wait(lambda s: len(s["invites"]) == 1, "Bob invite")["invites"][0]; log(f"Bob got invite {inv['instance']} world={inv['world']}")
B.cmd(cmd="reply_invite", **{"from": inv["from"], "id": inv["id"], "accept": True})
inv = B.wait(lambda s: s["invites"][0]["port"] and s["invites"][0]["world"], "Bob tunnel port")["invites"][0]; log(f"Bob tunnel on 127.0.0.1:{inv['port']}")
# LAN re-broadcast reaches a listener on this machine
ls = socket.socket(socket.AF_INET, socket.SOCK_DGRAM); ls.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1); ls.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEPORT, 1)
ls.bind(("", 4445)); ls.setsockopt(socket.IPPROTO_IP, socket.IP_ADD_MEMBERSHIP, struct.pack("4s4s", socket.inet_aton("224.0.2.60"), socket.inet_aton("0.0.0.0"))); ls.settimeout(5)
seen = None
end = time.time() + 6
while time.time() < end:
    try: d, src = ls.recvfrom(2048)
    except socket.timeout: break
    if f"[AD]{inv['port']}[/AD]" in d.decode(): seen = (d.decode(), src); break
log(f"LAN re-broadcast seen: {seen}")
c = socket.create_connection((os.environ.get("TUN_HOST") or (seen[1][0] if seen else "127.0.0.1"), inv["port"]), timeout=10)
payload = os.urandom(300000); c.sendall(payload); got = b""
while len(got) < len(payload): got += c.recv(65536)
c.close()
log(f"tunnel echo OK: {got == payload} ({len(got)} bytes)")
A.wait(lambda s: s["hosting"]["invites"][0]["to"][0]["state"] == "playing", "Alice sees Bob playing"); log("Alice sees Bob playing")
B.cmd(cmd="remove_friend", key=a["key"])
A.wait(lambda s: not s["friends"], "Alice friend removed"); log("unfriend propagated")
print("NOTICES A:", [n.get("kind", n["ev"]) for n in A.notices]); print("NOTICES B:", [n.get("kind", n["ev"]) for n in B.notices])
for p in procs: p.kill()
print("PASS" if got == payload and seen else "FAIL")
