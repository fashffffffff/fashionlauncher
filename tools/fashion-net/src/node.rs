// SPDX-License-Identifier: GPL-3.0-only
//! The friends node: DHT presence + inbox, dialing/hole punching, friend and invite state,
//! LAN world detection and TCP tunnels over QUIC.

use crate::lan;
use crate::proto::*;
use crate::sock::{DemuxSocket, Stun};
use crate::tls;
use anyhow::{anyhow, bail, Context as _, Result};
use ed25519_dalek::{Signature, Signer, SigningKey, Verifier, VerifyingKey};
use futures::StreamExt;
use mainline::async_dht::AsyncDht;
use mainline::{Id, MutableItem};
use quinn::{Connection, Endpoint, RecvStream, SendStream};
use serde_json::{json, Value};
use std::collections::{BTreeMap, HashMap, HashSet};
use std::net::{IpAddr, Ipv4Addr, Ipv6Addr, SocketAddr};
use std::path::PathBuf;
use std::sync::atomic::{AtomicUsize, Ordering};
use std::sync::{Arc, Mutex};
use std::time::{Duration, Instant, SystemTime, UNIX_EPOCH};
use tokio::io::{AsyncBufReadExt, AsyncWriteExt, BufReader};
use tokio::sync::{mpsc, Notify};

pub type Key = [u8; 32];

const POLL_EVERY: Duration = Duration::from_secs(15);
const PUBLISH_EVERY: Duration = Duration::from_secs(4 * 60);
const DIAL_TIMEOUT: Duration = Duration::from_secs(12);
const MAX_LINE: usize = 4 << 20;

pub fn now() -> i64 {
    SystemTime::now().duration_since(UNIX_EPOCH).unwrap().as_secs() as i64
}

#[derive(Clone)]
struct Friend {
    name: String,
    /// we asked them; they haven't accepted yet
    pending: bool,
}

#[derive(Default)]
struct Peer {
    presence: Option<Presence>,
    connecting: bool,
    failures: u32,
    reason: String,
    status: String,
    game: String,
}

struct Link {
    id: usize,
    conn: Connection,
    tx: mpsc::UnboundedSender<PeerMsg>,
    is_client: bool,
}

struct OutInvite {
    id: String,
    /// launcher-side instance id (never sent to peers)
    local_id: String,
    instance: String,
    mc: String,
    manifest: Value,
    /// key -> "sent" | "accepted" | "declined" | "playing"
    to: BTreeMap<Key, String>,
}

struct InInvite {
    id: String,
    from: Key,
    instance: String,
    mc: String,
    manifest: Value,
    /// "new" | "accepted"
    state: String,
    world: Option<World>,
    local_port: u16,
    tasks: Vec<tokio::task::AbortHandle>,
}

impl Drop for InInvite {
    fn drop(&mut self) {
        for t in &self.tasks {
            t.abort();
        }
    }
}

struct State {
    name: String,
    friends: BTreeMap<Key, Friend>,
    peers: HashMap<Key, Peer>,
    links: HashMap<Key, Link>,
    requests: BTreeMap<Key, String>,
    declined: HashSet<Key>,
    out_invites: Vec<OutInvite>,
    in_invites: Vec<InInvite>,
    lan_world: Option<(World, Instant)>,
    stun_public: Vec<SocketAddr>,
    nat: String,
    dht_ok: bool,
    local_v4: Vec<Ipv4Addr>,
    local_v6: Vec<Ipv6Addr>,
    status: String,
    game: String,
    inbox_dialed: HashMap<SocketAddr, Instant>,
}

pub struct Node {
    me: SigningKey,
    key: Key,
    data: PathBuf,
    st: Mutex<State>,
    endpoint: Endpoint,
    local_port: u16,
    stun: Stun,
    dht: AsyncDht,
    out: mpsc::UnboundedSender<String>,
    dirty: Notify,
    republish: Notify,
    poll_now: Notify,
    link_ids: AtomicUsize,
    first_publish: std::sync::atomic::AtomicBool,
    loopback: bool,
}

pub struct Options {
    pub data: PathBuf,
    pub name: String,
    pub bootstrap: Vec<String>,
    pub port: u16,
    pub loopback: bool,
    pub no_stun: bool,
}

fn load_identity(dir: &PathBuf) -> Result<SigningKey> {
    let p = dir.join("identity.key");
    if let Ok(s) = std::fs::read_to_string(&p) {
        if let Some(k) = key_from_hex(s.trim()) {
            return Ok(SigningKey::from_bytes(&k));
        }
    }
    let k = SigningKey::from_bytes(&rand::random());
    std::fs::write(&p, key_hex(&k.to_bytes()))?;
    #[cfg(unix)]
    {
        use std::os::unix::fs::PermissionsExt;
        let _ = std::fs::set_permissions(&p, std::fs::Permissions::from_mode(0o600));
    }
    Ok(k)
}

fn load_friends(dir: &PathBuf) -> BTreeMap<Key, Friend> {
    let mut out = BTreeMap::new();
    if let Ok(b) = std::fs::read(dir.join("friends.json")) {
        if let Ok(Value::Array(a)) = serde_json::from_slice::<Value>(&b) {
            for f in a {
                if let Some(k) = f["key"].as_str().and_then(key_from_hex) {
                    out.insert(
                        k,
                        Friend {
                            name: f["name"].as_str().unwrap_or("").to_string(),
                            pending: f["pending"].as_bool().unwrap_or(false),
                        },
                    );
                }
            }
        }
    }
    out
}

fn bind_socket(port: u16) -> std::io::Result<std::net::UdpSocket> {
    use socket2::{Domain, Protocol, Socket, Type};
    let try_v6 = || -> std::io::Result<std::net::UdpSocket> {
        let s = Socket::new(Domain::IPV6, Type::DGRAM, Some(Protocol::UDP))?;
        s.set_only_v6(false)?;
        s.bind(&SocketAddr::new(IpAddr::V6(Ipv6Addr::UNSPECIFIED), port).into())?;
        Ok(s.into())
    };
    try_v6().or_else(|_| std::net::UdpSocket::bind(SocketAddr::new(IpAddr::V4(Ipv4Addr::UNSPECIFIED), port)))
}

impl Node {
    pub async fn start(opts: Options, out: mpsc::UnboundedSender<String>) -> Result<Arc<Node>> {
        std::fs::create_dir_all(&opts.data)?;
        let me = load_identity(&opts.data)?;
        let key = me.verifying_key().to_bytes();

        let sock = bind_socket(opts.port).or_else(|_| bind_socket(0)).context("binding UDP socket")?;
        let local_port = sock.local_addr()?.port();
        let runtime = quinn::default_runtime().ok_or_else(|| anyhow!("no async runtime"))?;
        let (demux, stun) = DemuxSocket::wrap(runtime.wrap_udp_socket(sock)?);
        let mut endpoint = Endpoint::new_with_abstract_socket(
            tls::endpoint_config(),
            Some(tls::server_config()?),
            demux,
            runtime,
        )?;
        endpoint.set_default_client_config(tls::client_config()?);

        let mut b = mainline::Dht::builder();
        if !opts.bootstrap.is_empty() {
            b.bootstrap(&opts.bootstrap);
        }
        let dht = b.build()?.as_async();

        let (v4, v6) = lan::local_addrs();
        let node = Arc::new(Node {
            me,
            key,
            st: Mutex::new(State {
                name: opts.name,
                friends: load_friends(&opts.data),
                peers: HashMap::new(),
                links: HashMap::new(),
                requests: BTreeMap::new(),
                declined: HashSet::new(),
                out_invites: Vec::new(),
                in_invites: Vec::new(),
                lan_world: None,
                stun_public: Vec::new(),
                nat: "unknown".into(),
                dht_ok: false,
                local_v4: v4,
                local_v6: v6,
                status: "online".into(),
                game: String::new(),
                inbox_dialed: HashMap::new(),
            }),
            data: opts.data,
            endpoint,
            local_port,
            stun,
            dht,
            out,
            dirty: Notify::new(),
            republish: Notify::new(),
            poll_now: Notify::new(),
            link_ids: AtomicUsize::new(1),
            first_publish: Default::default(),
            loopback: opts.loopback,
        });

        tokio::spawn(node.clone().emit_loop());
        tokio::spawn(node.clone().accept_loop());
        if !opts.no_stun {
            tokio::spawn(node.clone().stun_loop());
        }
        tokio::spawn(node.clone().dht_loop());
        tokio::spawn(node.clone().publish_loop());
        tokio::spawn(node.clone().poll_loop());
        tokio::spawn(node.clone().lan_loop());
        node.dirty.notify_one();
        Ok(node)
    }

    // ---------- output ----------

    fn send_event(&self, v: Value) {
        let _ = self.out.send(v.to_string());
    }

    fn notice(&self, kind: &str, key: Option<&Key>, extra: Value) {
        let name = key.map(|k| self.peer_name(k)).unwrap_or_default();
        let mut v = json!({"ev": "notice", "kind": kind, "name": name, "key": key.map(key_hex)});
        if let (Value::Object(m), Value::Object(e)) = (&mut v, extra) {
            m.extend(e);
        }
        self.send_event(v);
    }

    fn peer_name(&self, k: &Key) -> String {
        let st = self.st.lock().unwrap();
        st.friends
            .get(k)
            .map(|f| f.name.clone())
            .or_else(|| st.requests.get(k).cloned())
            .filter(|n| !n.is_empty())
            .unwrap_or_else(|| encode_code(k)[..9].to_string())
    }

    fn touch(&self) {
        self.dirty.notify_one();
    }

    async fn emit_loop(self: Arc<Self>) {
        loop {
            self.dirty.notified().await;
            tokio::time::sleep(Duration::from_millis(60)).await;
            let snap = self.snapshot();
            self.send_event(snap);
        }
    }

    fn snapshot(&self) -> Value {
        let st = self.st.lock().unwrap();
        let t = now();
        let friends: Vec<Value> = st
            .friends
            .iter()
            .map(|(k, f)| {
                let p = st.peers.get(k);
                let link = st.links.get(k);
                let fresh = p.and_then(|p| p.presence.as_ref()).map(|pr| pr.is_fresh(t)).unwrap_or(false);
                let conn = if link.is_some() {
                    "direct"
                } else if p.map(|p| p.failures >= 2).unwrap_or(false) && fresh {
                    "failed"
                } else if p.map(|p| p.connecting).unwrap_or(false) {
                    "connecting"
                } else {
                    "none"
                };
                json!({
                    "key": key_hex(k),
                    "code": encode_code(k),
                    "name": if f.name.is_empty() { encode_code(k)[..9].to_string() } else { f.name.clone() },
                    "pending": f.pending,
                    "online": link.is_some() || fresh,
                    "conn": conn,
                    "reason": p.map(|p| p.reason.clone()).unwrap_or_default(),
                    "status": if link.is_some() { p.map(|p| p.status.clone()).unwrap_or_default() } else { String::new() },
                    "game": if link.is_some() { p.map(|p| p.game.clone()).unwrap_or_default() } else { String::new() },
                    "addr": link.map(|l| canon(l.conn.remote_address()).to_string()).unwrap_or_default(),
                })
            })
            .collect();
        let requests: Vec<Value> = st
            .requests
            .iter()
            .map(|(k, n)| json!({"key": key_hex(k), "code": encode_code(k), "name": n}))
            .collect();
        let name_of = |k: &Key| {
            st.friends.get(k).map(|f| f.name.clone()).filter(|n| !n.is_empty()).unwrap_or_else(|| encode_code(k)[..9].to_string())
        };
        let invites: Vec<Value> = st
            .in_invites
            .iter()
            .map(|i| {
                json!({
                    "from": key_hex(&i.from), "name": name_of(&i.from), "id": i.id,
                    "instance": i.instance, "mc": i.mc, "manifest": i.manifest,
                    "state": i.state, "world": i.world, "port": i.local_port,
                    "connected": st.links.contains_key(&i.from),
                })
            })
            .collect();
        let hosting: Vec<Value> = st
            .out_invites
            .iter()
            .map(|i| {
                json!({
                    "id": i.id, "instance": i.instance, "mc": i.mc, "instanceId": i.local_id,
                    "to": i.to.iter().map(|(k, s)| json!({"key": key_hex(k), "name": name_of(k), "state": s})).collect::<Vec<_>>(),
                })
            })
            .collect();
        json!({
            "ev": "state",
            "me": {"key": key_hex(&self.key), "code": encode_code(&self.key), "link": make_link(&self.key, &st.name), "name": st.name},
            "net": {
                "dht": st.dht_ok, "nat": st.nat,
                "public": st.stun_public.first().map(|a| a.to_string()).unwrap_or_default(),
                "port": self.local_port,
            },
            "friends": friends,
            "requests": requests,
            "invites": invites,
            "hosting": {"world": st.lan_world.as_ref().map(|w| &w.0), "invites": hosting},
        })
    }

    fn save_friends(&self) {
        let list: Vec<Value> = {
            let st = self.st.lock().unwrap();
            st.friends
                .iter()
                .map(|(k, f)| json!({"key": key_hex(k), "name": f.name, "pending": f.pending}))
                .collect()
        };
        let tmp = self.data.join("friends.json.tmp");
        if std::fs::write(&tmp, serde_json::to_vec_pretty(&list).unwrap()).is_ok() {
            let _ = std::fs::rename(&tmp, self.data.join("friends.json"));
        }
    }

    // ---------- addresses ----------

    fn candidates(&self) -> Vec<SocketAddr> {
        let st = self.st.lock().unwrap();
        let mut v: Vec<SocketAddr> = st.stun_public.clone();
        for a in &st.local_v6 {
            v.push(SocketAddr::new(IpAddr::V6(*a), self.local_port));
        }
        for a in &st.local_v4 {
            v.push(SocketAddr::new(IpAddr::V4(*a), self.local_port));
        }
        if self.loopback {
            v.push(SocketAddr::new(IpAddr::V4(Ipv4Addr::LOCALHOST), self.local_port));
        }
        let mut seen = HashSet::new();
        v.retain(|a| seen.insert(*a));
        v.truncate(10);
        v
    }

    async fn stun_loop(self: Arc<Self>) {
        let hosts = ["stun.l.google.com:19302", "stun.cloudflare.com:3478", "stun1.l.google.com:19302"];
        loop {
            let (v4, v6) = lan::local_addrs();
            {
                let mut st = self.st.lock().unwrap();
                st.local_v4 = v4.clone();
                st.local_v6 = v6;
            }
            let mut servers4 = Vec::new();
            let mut server6 = None;
            for h in hosts {
                if let Ok(addrs) = tokio::net::lookup_host(h).await {
                    for a in addrs {
                        if a.is_ipv4() && servers4.len() < 2 && !servers4.iter().any(|s: &SocketAddr| s.ip() == a.ip()) {
                            servers4.push(a);
                        } else if a.is_ipv6() && server6.is_none() {
                            server6 = Some(a);
                        }
                    }
                }
            }
            let mut mapped4 = Vec::new();
            for s in &servers4 {
                if let Some(a) = self.stun.query(*s).await {
                    mapped4.push(a);
                }
            }
            let mapped6 = match server6 {
                Some(s) => self.stun.query(s).await,
                None => None,
            };
            let nat = match mapped4.as_slice() {
                [] => "unknown",
                [a, rest @ ..] => {
                    if rest.iter().any(|b| b != a) {
                        "hard"
                    } else if a.port() == self.local_port && matches!(a.ip(), IpAddr::V4(ip) if v4.contains(&ip)) {
                        "open"
                    } else {
                        "easy"
                    }
                }
            };
            let mut public: Vec<SocketAddr> = Vec::new();
            if let Some(a) = mapped4.first() {
                public.push(*a);
            }
            if let Some(a) = mapped6 {
                public.push(a);
            }
            let changed = {
                let mut st = self.st.lock().unwrap();
                let changed = st.stun_public != public || st.nat != nat;
                st.stun_public = public;
                st.nat = nat.to_string();
                changed
            };
            if changed {
                eprintln!("[net] nat={nat} public={:?}", self.st.lock().unwrap().stun_public);
                self.republish.notify_one();
                self.touch();
            }
            tokio::time::sleep(Duration::from_secs(120)).await;
        }
    }

    // ---------- DHT ----------

    async fn dht_loop(self: Arc<Self>) {
        loop {
            let ok = self.dht.bootstrapped().await;
            let changed = {
                let mut st = self.st.lock().unwrap();
                let c = st.dht_ok != ok;
                st.dht_ok = ok;
                c
            };
            if changed {
                eprintln!("[dht] bootstrapped={ok}");
                self.touch();
                if ok {
                    self.republish.notify_one();
                    self.poll_now.notify_one();
                }
            }
            tokio::time::sleep(Duration::from_secs(if ok { 60 } else { 10 })).await;
        }
    }

    fn presence_record(&self, online: bool) -> Presence {
        let st = self.st.lock().unwrap();
        Presence {
            v: PROTO_VERSION,
            n: st.name.clone(),
            e: vec![],
            t: now(),
            s: if online { "on" } else { "off" }.into(),
            x: st.nat.clone(),
        }
    }

    async fn publish(&self, online: bool) {
        let mut p = self.presence_record(online);
        p.e = self.candidates().iter().map(|a| a.to_string()).collect();
        let seq = SystemTime::now().duration_since(UNIX_EPOCH).unwrap().as_millis() as i64;
        let item = MutableItem::new(self.me.clone(), &p.encode(), seq, None);
        match tokio::time::timeout(Duration::from_secs(30), self.dht.put_mutable(item, None)).await {
            Ok(Ok(_)) => {
                eprintln!("[dht] published presence ({} endpoints)", p.e.len());
                // friends who started at the same time looked us up too early: look again now
                if online && !self.first_publish.swap(true, Ordering::Relaxed) {
                    self.poll_now.notify_one();
                }
            }
            Ok(Err(e)) => eprintln!("[dht] publish failed: {e:?}"),
            Err(_) => eprintln!("[dht] publish timed out"),
        }
    }

    async fn publish_loop(self: Arc<Self>) {
        // give STUN a moment so the first record already has the public address
        tokio::time::sleep(Duration::from_secs(3)).await;
        loop {
            self.publish(true).await;
            tokio::select! {
                _ = tokio::time::sleep(PUBLISH_EVERY) => {}
                _ = self.republish.notified() => { tokio::time::sleep(Duration::from_secs(1)).await; }
            }
        }
    }

    pub async fn sign_off(&self) {
        let _ = tokio::time::timeout(Duration::from_secs(4), self.publish(false)).await;
    }

    async fn poll_loop(self: Arc<Self>) {
        let mut round: u64 = 0;
        loop {
            let (targets, pending): (Vec<Key>, Vec<Key>) = {
                let st = self.st.lock().unwrap();
                let t: Vec<Key> = st
                    .friends
                    .keys()
                    .filter(|k| !st.links.contains_key(*k) && !st.peers.get(*k).map(|p| p.connecting).unwrap_or(false))
                    .cloned()
                    .collect();
                let p: Vec<Key> = st.friends.iter().filter(|(k, f)| f.pending && !st.links.contains_key(*k)).map(|(k, _)| *k).collect();
                (t, p)
            };
            for k in targets {
                tokio::spawn(self.clone().lookup_and_dial(k));
            }
            // tell people we asked that we're knocking (lets them dial back through our NAT)
            if round % 4 == 0 {
                let port = self.st.lock().unwrap().stun_public.iter().find(|a| a.is_ipv4()).map(|a| a.port()).unwrap_or(self.local_port);
                for k in pending {
                    let dht = self.dht.clone();
                    tokio::spawn(async move {
                        let id = Id::from_bytes(inbox_hash(&k)).unwrap();
                        let _ = tokio::time::timeout(Duration::from_secs(30), dht.announce_peer(id, Some(port))).await;
                    });
                }
            }
            tokio::spawn(self.clone().check_inbox());
            round += 1;
            tokio::select! {
                _ = tokio::time::sleep(POLL_EVERY) => {}
                _ = self.poll_now.notified() => {}
            }
        }
    }

    async fn lookup_and_dial(self: Arc<Self>, k: Key) {
        {
            let mut st = self.st.lock().unwrap();
            let p = st.peers.entry(k).or_default();
            if p.connecting {
                return;
            }
            p.connecting = true;
        }
        self.touch();
        let item = tokio::time::timeout(Duration::from_secs(25), self.dht.get_mutable_most_recent(&k, None))
            .await
            .ok()
            .flatten();
        let presence = item.and_then(|i| Presence::decode(i.value()));
        let fresh = presence.as_ref().map(|p| p.is_fresh(now())).unwrap_or(false);
        let mut cands = presence.as_ref().map(|p| p.endpoints()).unwrap_or_default();
        {
            let mut st = self.st.lock().unwrap();
            if let Some(p) = &presence {
                if let Some(f) = st.friends.get_mut(&k) {
                    if f.name.is_empty() && !p.n.is_empty() {
                        f.name = p.n.chars().take(32).collect();
                    }
                }
            }
            let peer = st.peers.entry(k).or_default();
            if presence.is_some() {
                peer.presence = presence.clone();
            }
            if !fresh {
                peer.connecting = false;
                peer.failures = 0;
                peer.reason = "offline".into();
            }
        }
        if !fresh {
            self.touch();
            return;
        }
        cands.retain(|a| !a.ip().is_unspecified());
        let res = self.clone().dial_many(cands, Some(k)).await;
        let my_nat = self.st.lock().unwrap().nat.clone();
        {
            let mut st = self.st.lock().unwrap();
            let peer = st.peers.entry(k).or_default();
            peer.connecting = false;
            match &res {
                Ok(_) => {
                    peer.failures = 0;
                    peer.reason.clear();
                }
                Err(e) => {
                    peer.failures += 1;
                    let theirs = presence.as_ref().map(|p| p.x.as_str()).unwrap_or("unknown");
                    peer.reason = if my_nat == "hard" && theirs == "hard" {
                        "both_strict"
                    } else if my_nat == "hard" || theirs == "hard" {
                        "one_strict"
                    } else {
                        "unreachable"
                    }
                    .into();
                    eprintln!("[dial] {} failed: {e}", &encode_code(&k)[..9]);
                }
            }
        }
        let notify = {
            let st = self.st.lock().unwrap();
            st.peers.get(&k).filter(|p| res.is_err() && p.failures == 2).map(|p| p.reason.clone())
        };
        if let Some(reason) = notify {
            self.notice("unreachable", Some(&k), json!({"reason": reason}));
        }
        self.touch();
    }

    async fn check_inbox(self: Arc<Self>) {
        let id = Id::from_bytes(inbox_hash(&self.key)).unwrap();
        let mut stream = self.dht.get_peers(id);
        let mut addrs: Vec<SocketAddr> = Vec::new();
        let _ = tokio::time::timeout(Duration::from_secs(20), async {
            while let Some(batch) = stream.next().await {
                for a in batch {
                    let a = SocketAddr::V4(a);
                    if !addrs.contains(&a) {
                        addrs.push(a);
                    }
                }
            }
        })
        .await;
        let fresh: Vec<SocketAddr> = {
            let mut st = self.st.lock().unwrap();
            st.inbox_dialed.retain(|_, t| t.elapsed() < Duration::from_secs(60));
            let mine: HashSet<SocketAddr> = st.stun_public.iter().cloned().collect();
            addrs
                .into_iter()
                .filter(|a| !mine.contains(a) && !st.inbox_dialed.contains_key(a))
                .take(8)
                .collect()
        };
        for a in fresh {
            self.st.lock().unwrap().inbox_dialed.insert(a, Instant::now());
            let me = self.clone();
            tokio::spawn(async move {
                let _ = me.dial_many(vec![a], None).await;
            });
        }
    }

    // ---------- connections ----------

    async fn dial_many(self: Arc<Self>, cands: Vec<SocketAddr>, expect: Option<Key>) -> Result<Key> {
        if cands.is_empty() {
            bail!("no addresses");
        }
        let (tx, mut rx) = mpsc::channel(cands.len());
        let mut set = tokio::task::JoinSet::new();
        for a in cands.clone() {
            let me = self.clone();
            let tx = tx.clone();
            set.spawn(async move {
                let r = tokio::time::timeout(DIAL_TIMEOUT, me.dial_one(a, expect)).await;
                let r = r.map_err(|_| anyhow!("timed out")).and_then(|r| r);
                let _ = tx.send((a, r)).await;
            });
        }
        drop(tx);
        let mut last = anyhow!("no answer");
        while let Some((a, r)) = rx.recv().await {
            match r {
                Ok(k) => {
                    eprintln!("[dial] connected to {} via {a}", &encode_code(&k)[..9]);
                    set.abort_all();
                    return Ok(k);
                }
                Err(e) => last = e.context(a.to_string()),
            }
        }
        Err(last)
    }

    async fn dial_one(self: Arc<Self>, addr: SocketAddr, expect: Option<Key>) -> Result<Key> {
        let conn = self.endpoint.connect(addr, "fashion")?.await?;
        let (mut send, recv) = conn.open_bi().await?;
        let mut recv = BufReader::new(recv);
        write_msg(&mut send, &self.hello(&conn, true)?).await?;
        let peer = match read_msg(&mut recv).await? {
            PeerMsg::Hello { ver: _, key, name, sig } => verify_hello(&conn, &key, &sig, false).map(|k| (k, name))?,
            _ => bail!("expected hello"),
        };
        if let Some(e) = expect {
            if e != peer.0 {
                conn.close(1u32.into(), b"wrong peer");
                bail!("a different person answered");
            }
        }
        self.register(conn, peer.0, peer.1, true, send, recv)
    }

    async fn accept_loop(self: Arc<Self>) {
        while let Some(inc) = self.endpoint.accept().await {
            let me = self.clone();
            tokio::spawn(async move {
                let r: Result<Key> = async {
                    let conn = tokio::time::timeout(DIAL_TIMEOUT, inc).await??;
                    let (mut send, recv) = tokio::time::timeout(Duration::from_secs(10), conn.accept_bi()).await??;
                    let mut recv = BufReader::new(recv);
                    let peer = match tokio::time::timeout(Duration::from_secs(10), read_msg(&mut recv)).await?? {
                        PeerMsg::Hello { key, name, sig, .. } => (verify_hello(&conn, &key, &sig, true)?, name),
                        _ => bail!("expected hello"),
                    };
                    write_msg(&mut send, &me.hello(&conn, false)?).await?;
                    me.clone().register(conn, peer.0, peer.1, false, send, recv)
                }
                .await;
                if let Err(e) = r {
                    eprintln!("[accept] {e:#}");
                }
            });
        }
    }

    fn hello(&self, conn: &Connection, is_client: bool) -> Result<PeerMsg> {
        let mut exp = [0u8; 32];
        conn.export_keying_material(&mut exp, b"fashion-net", b"").map_err(|_| anyhow!("exporter"))?;
        let sig = self.me.sign(&auth_message(&exp, is_client));
        Ok(PeerMsg::Hello {
            ver: PROTO_VERSION,
            key: key_hex(&self.key),
            name: self.st.lock().unwrap().name.clone(),
            sig: data_encoding::HEXLOWER.encode(&sig.to_bytes()),
        })
    }

    fn register(
        self: Arc<Self>,
        conn: Connection,
        peer: Key,
        hello_name: String,
        is_client: bool,
        send: SendStream,
        recv: BufReader<RecvStream>,
    ) -> Result<Key> {
        if peer == self.key {
            conn.close(2u32.into(), b"self");
            bail!("that's us");
        }
        let id = self.link_ids.fetch_add(1, Ordering::Relaxed);
        let (tx, rx) = mpsc::unbounded_channel();
        let hello_name: String = hello_name.chars().take(32).collect();
        {
            let mut st = self.st.lock().unwrap();
            if st.declined.contains(&peer) {
                conn.close(3u32.into(), b"declined");
                bail!("declined earlier");
            }
            // two connections may race (both sides dial): keep the one dialed by the smaller key
            let preferred = |client_side: bool| client_side == (self.key < peer);
            if let Some(old) = st.links.get(&peer) {
                if preferred(old.is_client) || !preferred(is_client) {
                    conn.close(4u32.into(), b"duplicate");
                    return Ok(peer);
                }
                old.conn.close(4u32.into(), b"duplicate");
            }
            st.links.insert(peer, Link { id, conn: conn.clone(), tx: tx.clone(), is_client });
            if let Some(f) = st.friends.get_mut(&peer) {
                if !hello_name.is_empty() {
                    f.name = hello_name.clone();
                }
            }
            if let Some(n) = st.requests.get_mut(&peer) {
                *n = hello_name.clone();
            }
            let p = st.peers.entry(peer).or_default();
            p.failures = 0;
            p.reason.clear();
            p.status = "online".into();
        }
        tokio::spawn(writer(send, rx));
        tokio::spawn(self.clone().reader(peer, id, conn.clone(), recv, hello_name));
        tokio::spawn(self.clone().stream_loop(peer, conn));
        self.on_linked(peer);
        self.touch();
        Ok(peer)
    }

    fn on_linked(&self, peer: Key) {
        let st = self.st.lock().unwrap();
        let Some(link) = st.links.get(&peer) else { return };
        match st.friends.get(&peer) {
            Some(f) if f.pending => {
                let _ = link.tx.send(PeerMsg::Add);
            }
            Some(_) => {
                let _ = link.tx.send(PeerMsg::Presence { status: self.status_of(&st), game: self.game_of(&st) });
                for inv in &st.out_invites {
                    if let Some(s) = inv.to.get(&peer) {
                        if s != "declined" {
                            let _ = link.tx.send(self.invite_msg(inv, &st));
                        }
                    }
                }
            }
            None => {}
        }
    }

    fn status_of(&self, st: &State) -> String {
        if st.lan_world.is_some() {
            "hosting".into()
        } else {
            st.status.clone()
        }
    }
    fn game_of(&self, st: &State) -> String {
        match &st.lan_world {
            Some((w, _)) => w.motd.clone(),
            None => st.game.clone(),
        }
    }

    fn invite_msg(&self, inv: &OutInvite, st: &State) -> PeerMsg {
        PeerMsg::Invite {
            id: inv.id.clone(),
            instance: inv.instance.clone(),
            mc: inv.mc.clone(),
            manifest: inv.manifest.clone(),
            world: st.lan_world.as_ref().map(|w| w.0.clone()),
        }
    }

    async fn reader(self: Arc<Self>, peer: Key, id: usize, conn: Connection, mut recv: BufReader<RecvStream>, name: String) {
        loop {
            match read_msg(&mut recv).await {
                Ok(m) => self.handle(peer, m, &name),
                Err(e) => {
                    eprintln!("[link] {} closed: {e:#}", &encode_code(&peer)[..9]);
                    break;
                }
            }
        }
        conn.close(0u32.into(), b"bye");
        let removed = {
            let mut st = self.st.lock().unwrap();
            if st.links.get(&peer).map(|l| l.id == id).unwrap_or(false) {
                st.links.remove(&peer);
                if let Some(p) = st.peers.get_mut(&peer) {
                    p.status.clear();
                }
                true
            } else {
                false
            }
        };
        if removed {
            self.touch();
            // they may come back quickly (network change): look again soon
            tokio::time::sleep(Duration::from_secs(2)).await;
            self.poll_now.notify_one();
        }
    }

    fn send_to(&self, peer: &Key, m: PeerMsg) -> bool {
        let st = self.st.lock().unwrap();
        st.links.get(peer).map(|l| l.tx.send(m).is_ok()).unwrap_or(false)
    }

    fn drop_link(&self, peer: &Key, reason: &[u8]) {
        if let Some(l) = self.st.lock().unwrap().links.remove(peer) {
            // let the writer flush the last message first
            let c = l.conn.clone();
            let reason = reason.to_vec();
            tokio::spawn(async move {
                tokio::time::sleep(Duration::from_millis(300)).await;
                c.close(5u32.into(), &reason);
            });
        }
    }

    fn handle(self: &Arc<Self>, peer: Key, m: PeerMsg, hello_name: &str) {
        match m {
            PeerMsg::Add => {
                let (known, declined) = {
                    let st = self.st.lock().unwrap();
                    (st.friends.get(&peer).cloned(), st.declined.contains(&peer))
                };
                if declined {
                    self.send_to(&peer, PeerMsg::Declined);
                } else if let Some(f) = known {
                    if f.pending {
                        self.st.lock().unwrap().friends.get_mut(&peer).unwrap().pending = false;
                        self.save_friends();
                        self.notice("accepted", Some(&peer), json!({}));
                    }
                    self.send_to(&peer, PeerMsg::Accepted);
                    self.on_linked(peer);
                } else {
                    let new = self.st.lock().unwrap().requests.insert(peer, hello_name.to_string()).is_none();
                    if new {
                        self.notice("request", Some(&peer), json!({}));
                    }
                }
            }
            PeerMsg::Accepted => {
                let was_pending = {
                    let mut st = self.st.lock().unwrap();
                    match st.friends.get_mut(&peer) {
                        Some(f) if f.pending => {
                            f.pending = false;
                            true
                        }
                        _ => false,
                    }
                };
                if was_pending {
                    self.save_friends();
                    self.notice("accepted", Some(&peer), json!({}));
                    self.on_linked(peer);
                }
            }
            PeerMsg::Declined => {
                let removed = self.st.lock().unwrap().friends.remove(&peer).is_some();
                if removed {
                    self.save_friends();
                    self.notice("declined", Some(&peer), json!({}));
                }
                self.drop_link(&peer, b"declined");
            }
            PeerMsg::Unfriend => {
                let name = self.peer_name(&peer);
                let removed = self.forget(&peer);
                if removed {
                    self.notice("removed", None, json!({"name": name}));
                }
                self.drop_link(&peer, b"unfriend");
            }
            PeerMsg::Name { name } => {
                let mut st = self.st.lock().unwrap();
                let name: String = name.chars().take(32).collect();
                if let Some(f) = st.friends.get_mut(&peer) {
                    f.name = name.clone();
                }
                if let Some(n) = st.requests.get_mut(&peer) {
                    *n = name;
                }
            }
            PeerMsg::Presence { status, game } => {
                let is_friend = self.st.lock().unwrap().friends.get(&peer).map(|f| !f.pending).unwrap_or(false);
                if !is_friend {
                    // they think we're friends but we removed them
                    let pending = self.st.lock().unwrap().friends.contains_key(&peer);
                    if !pending && !self.st.lock().unwrap().requests.contains_key(&peer) {
                        self.send_to(&peer, PeerMsg::Unfriend);
                        self.drop_link(&peer, b"unfriend");
                    }
                    return;
                }
                let mut st = self.st.lock().unwrap();
                let p = st.peers.entry(peer).or_default();
                p.status = status.chars().take(16).collect();
                p.game = game.chars().take(64).collect();
            }
            PeerMsg::Invite { id, instance, mc, manifest, world } => {
                let is_friend = self.st.lock().unwrap().friends.get(&peer).map(|f| !f.pending).unwrap_or(false);
                if !is_friend {
                    return;
                }
                let new = {
                    let mut st = self.st.lock().unwrap();
                    if let Some(i) = st.in_invites.iter_mut().find(|i| i.from == peer && i.id == id) {
                        i.world = world;
                        i.instance = instance;
                        i.mc = mc;
                        i.manifest = manifest;
                        false
                    } else {
                        st.in_invites.push(InInvite {
                            id,
                            from: peer,
                            instance: instance.chars().take(64).collect(),
                            mc: mc.chars().take(32).collect(),
                            manifest,
                            state: "new".into(),
                            world,
                            local_port: 0,
                            tasks: vec![],
                        });
                        true
                    }
                };
                if new {
                    let inst = instance_name(&self.st.lock().unwrap(), &peer);
                    self.notice("invite", Some(&peer), json!({"instance": inst}));
                }
            }
            PeerMsg::InviteCancel { id } => {
                let removed = {
                    let mut st = self.st.lock().unwrap();
                    let before = st.in_invites.len();
                    st.in_invites.retain(|i| !(i.from == peer && i.id == id));
                    before != st.in_invites.len()
                };
                if removed {
                    self.notice("invite_cancelled", Some(&peer), json!({}));
                }
            }
            PeerMsg::InviteReply { id, accept } => {
                let world = {
                    let mut st = self.st.lock().unwrap();
                    let w = st.lan_world.as_ref().map(|w| w.0.clone());
                    match st.out_invites.iter_mut().find(|i| i.id == id) {
                        Some(inv) if inv.to.contains_key(&peer) => {
                            inv.to.insert(peer, if accept { "accepted" } else { "declined" }.into());
                            Some(w)
                        }
                        _ => None,
                    }
                };
                if let Some(w) = world {
                    self.notice(if accept { "invite_accepted" } else { "invite_declined" }, Some(&peer), json!({}));
                    if accept {
                        self.send_to(&peer, PeerMsg::World { id, world: w });
                    }
                }
            }
            PeerMsg::World { id, world } => {
                let opened = {
                    let mut st = self.st.lock().unwrap();
                    match st.in_invites.iter_mut().find(|i| i.from == peer && i.id == id) {
                        Some(i) => {
                            let opened = i.world.is_none() && world.is_some() && i.state == "accepted";
                            i.world = world;
                            opened
                        }
                        None => false,
                    }
                };
                if opened {
                    self.notice("world_ready", Some(&peer), json!({}));
                }
            }
            PeerMsg::Hello { .. } | PeerMsg::Tcp { .. } => {}
        }
        self.touch();
    }

    fn forget(&self, peer: &Key) -> bool {
        let mut st = self.st.lock().unwrap();
        let removed = st.friends.remove(peer).is_some();
        st.in_invites.retain(|i| &i.from != peer);
        for inv in &mut st.out_invites {
            inv.to.remove(peer);
        }
        st.peers.remove(peer);
        drop(st);
        if removed {
            self.save_friends();
        }
        removed
    }

    // ---------- tunnels ----------

    /// Incoming QUIC streams after the control stream: TCP tunnels into our LAN world.
    async fn stream_loop(self: Arc<Self>, peer: Key, conn: Connection) {
        while let Ok((send, recv)) = conn.accept_bi().await {
            let me = self.clone();
            tokio::spawn(async move {
                if let Err(e) = me.serve_tunnel(peer, send, recv).await {
                    eprintln!("[tunnel] {e:#}");
                }
            });
        }
    }

    async fn serve_tunnel(self: Arc<Self>, peer: Key, send: SendStream, recv: RecvStream) -> Result<()> {
        let mut recv = BufReader::new(recv);
        let PeerMsg::Tcp { id } = read_msg(&mut recv).await? else { bail!("expected tcp header") };
        let port = {
            let mut st = self.st.lock().unwrap();
            let port = st.lan_world.as_ref().map(|w| w.0.port);
            let inv = st.out_invites.iter_mut().find(|i| i.id == id);
            match (inv, port) {
                (Some(inv), Some(port)) if matches!(inv.to.get(&peer).map(|s| s.as_str()), Some("accepted" | "playing")) => {
                    inv.to.insert(peer, "playing".into());
                    port
                }
                _ => bail!("tunnel refused: no accepted invite or no open world"),
            }
        };
        self.touch();
        let tcp = tokio::net::TcpStream::connect((Ipv4Addr::LOCALHOST, port)).await?;
        let _ = tcp.set_nodelay(true);
        self.notice("joined", Some(&peer), json!({}));
        pipe(tcp, send, recv).await;
        Ok(())
    }

    async fn start_session(self: Arc<Self>, from: Key, id: String) -> Result<()> {
        let listener = tokio::net::TcpListener::bind((Ipv4Addr::UNSPECIFIED, 0)).await?;
        let port = listener.local_addr()?.port();
        eprintln!("[tunnel] joined {}: game can connect to 127.0.0.1:{port}", &encode_code(&from)[..9]);
        let me = self.clone();
        let (from2, id2) = (from, id.clone());
        let accept = tokio::spawn(async move {
            while let Ok((tcp, addr)) = listener.accept().await {
                eprintln!("[tunnel] game connected from {addr}");
                // same computer: loopback, or the peer address equals our own end of the socket
                let local = addr.ip().to_canonical().is_loopback()
                    || tcp.local_addr().map(|l| l.ip().to_canonical() == addr.ip().to_canonical()).unwrap_or(false);
                if !local {
                    eprintln!("[tunnel] refusing connection from {addr}");
                    continue; // only the game on this computer may use the tunnel
                }
                let conn = me.st.lock().unwrap().links.get(&from2).map(|l| l.conn.clone());
                let id = id2.clone();
                let me2 = me.clone();
                tokio::spawn(async move {
                    let Some(conn) = conn else {
                        me2.notice("tunnel_failed", Some(&from2), json!({}));
                        return;
                    };
                    let r: Result<()> = async {
                        let (mut send, recv) = conn.open_bi().await?;
                        write_msg(&mut send, &PeerMsg::Tcp { id }).await?;
                        let _ = tcp.set_nodelay(true);
                        pipe(tcp, send, BufReader::new(recv)).await;
                        Ok(())
                    }
                    .await;
                    if let Err(e) = r {
                        eprintln!("[tunnel] {e:#}");
                    }
                });
            }
        });
        let me = self.clone();
        let id3 = id.clone();
        let announce = tokio::spawn(async move {
            let Ok(sock) = lan::sender() else { return };
            loop {
                let motd = {
                    let st = me.st.lock().unwrap();
                    st.in_invites.iter().find(|i| i.from == from && i.id == id3).and_then(|i| i.world.as_ref().map(|w| w.motd.clone()))
                };
                if let Some(m) = motd {
                    lan::announce(&sock, &format_lan(&m, port)).await;
                }
                tokio::time::sleep(Duration::from_millis(1500)).await;
            }
        });
        let mut st = self.st.lock().unwrap();
        match st.in_invites.iter_mut().find(|i| i.from == from && i.id == id) {
            Some(i) => {
                i.local_port = port;
                i.tasks.push(accept.abort_handle());
                i.tasks.push(announce.abort_handle());
            }
            None => {
                accept.abort();
                announce.abort();
            }
        }
        Ok(())
    }

    async fn lan_loop(self: Arc<Self>) {
        let (v4, _) = lan::local_addrs();
        let sock = match lan::listener(&v4) {
            Ok(s) => s,
            Err(e) => {
                eprintln!("[lan] can't listen for LAN worlds: {e}");
                return;
            }
        };
        eprintln!("[lan] listening for LAN worlds on {:?}", sock.local_addr());
        let mut buf = vec![0u8; 2048];
        let mut tick = tokio::time::interval(Duration::from_secs(2));
        loop {
            tokio::select! {
                r = sock.recv_from(&mut buf) => {
                    let Ok((n, from)) = r else { continue };
                    let local = { let st = self.st.lock().unwrap(); lan::is_local_ip(from.ip(), &st.local_v4, &st.local_v6) };
                    if !local { eprintln!("[lan] ignoring announcement from {from}"); continue; }
                    let Some(w) = parse_lan(&String::from_utf8_lossy(&buf[..n])) else { continue };
                    let changed = {
                        let mut st = self.st.lock().unwrap();
                        let changed = st.lan_world.as_ref().map(|o| o.0 != w).unwrap_or(true);
                        st.lan_world = Some((w, Instant::now()));
                        changed
                    };
                    if changed { self.world_changed(); }
                }
                _ = tick.tick() => {
                    let gone = {
                        let mut st = self.st.lock().unwrap();
                        if st.lan_world.as_ref().map(|w| w.1.elapsed() > Duration::from_secs(6)).unwrap_or(false) {
                            st.lan_world = None;
                            true
                        } else { false }
                    };
                    if gone { self.world_changed(); }
                }
            }
        }
    }

    fn world_changed(&self) {
        let st = self.st.lock().unwrap();
        let w = st.lan_world.as_ref().map(|w| w.0.clone());
        eprintln!("[lan] world now {w:?}");
        for inv in &st.out_invites {
            for (k, s) in &inv.to {
                if s == "accepted" || s == "playing" {
                    if let Some(l) = st.links.get(k) {
                        let _ = l.tx.send(PeerMsg::World { id: inv.id.clone(), world: w.clone() });
                    }
                }
            }
        }
        let (status, game) = (self.status_of(&st), self.game_of(&st));
        for (k, l) in &st.links {
            if st.friends.get(k).map(|f| !f.pending).unwrap_or(false) {
                let _ = l.tx.send(PeerMsg::Presence { status: status.clone(), game: game.clone() });
            }
        }
        drop(st);
        self.touch();
    }

    // ---------- commands from the launcher ----------

    pub async fn command(self: &Arc<Self>, c: Value) -> Result<()> {
        let cmd = c["cmd"].as_str().unwrap_or("");
        let key_arg = |name: &str| c[name].as_str().and_then(key_from_hex).ok_or_else(|| anyhow!("bad key"));
        match cmd {
            "set_name" => {
                let name: String = c["name"].as_str().unwrap_or("").trim().chars().take(32).collect();
                {
                    let mut st = self.st.lock().unwrap();
                    if st.name == name {
                        return Ok(());
                    }
                    st.name = name.clone();
                    for l in st.links.values() {
                        let _ = l.tx.send(PeerMsg::Name { name: name.clone() });
                    }
                }
                self.republish.notify_one();
            }
            "add_friend" => {
                let Some((k, name)) = parse_code(c["code"].as_str().unwrap_or("")) else {
                    self.notice("bad_code", None, json!({}));
                    return Ok(());
                };
                if k == self.key {
                    self.notice("own_code", None, json!({}));
                    return Ok(());
                }
                let already = {
                    let mut st = self.st.lock().unwrap();
                    st.declined.remove(&k);
                    if st.friends.contains_key(&k) {
                        true
                    } else if st.requests.remove(&k).is_some() {
                        // they already asked us: adding them back accepts
                        st.friends.insert(k, Friend { name: name.clone().unwrap_or_default(), pending: false });
                        false
                    } else {
                        st.friends.insert(k, Friend { name: name.clone().unwrap_or_default(), pending: true });
                        false
                    }
                };
                if already {
                    self.notice("already_friend", Some(&k), json!({}));
                    return Ok(());
                }
                self.save_friends();
                if !self.send_to(&k, PeerMsg::Add) {
                    self.poll_now.notify_one();
                }
                self.notice("request_sent", Some(&k), json!({}));
            }
            "remove_friend" => {
                let k = key_arg("key")?;
                self.send_to(&k, PeerMsg::Unfriend);
                self.forget(&k);
                self.drop_link(&k, b"unfriend");
            }
            "accept_request" => {
                let k = key_arg("key")?;
                let name = self.st.lock().unwrap().requests.remove(&k);
                if let Some(name) = name {
                    self.st.lock().unwrap().friends.insert(k, Friend { name, pending: false });
                    self.save_friends();
                    self.send_to(&k, PeerMsg::Accepted);
                    self.on_linked(k);
                }
            }
            "decline_request" => {
                let k = key_arg("key")?;
                {
                    let mut st = self.st.lock().unwrap();
                    st.requests.remove(&k);
                    st.declined.insert(k);
                }
                self.send_to(&k, PeerMsg::Declined);
                self.drop_link(&k, b"declined");
            }
            "set_status" => {
                let mut st = self.st.lock().unwrap();
                st.status = c["status"].as_str().unwrap_or("online").chars().take(16).collect();
                st.game = c["game"].as_str().unwrap_or("").chars().take(64).collect();
                let (s, g) = (self.status_of(&st), self.game_of(&st));
                for (k, l) in &st.links {
                    if st.friends.get(k).map(|f| !f.pending).unwrap_or(false) {
                        let _ = l.tx.send(PeerMsg::Presence { status: s.clone(), game: g.clone() });
                    }
                }
            }
            "invite" => {
                let id = format!("{:016x}", rand::random::<u64>());
                let keys: Vec<Key> = c["keys"].as_array().map(|a| a.iter().filter_map(|v| v.as_str().and_then(key_from_hex)).collect()).unwrap_or_default();
                let inv = &c["invite"];
                let mut st = self.st.lock().unwrap();
                let o = OutInvite {
                    id: id.clone(),
                    local_id: inv["instanceId"].as_str().unwrap_or("").into(),
                    instance: inv["instance"].as_str().unwrap_or("").into(),
                    mc: inv["mc"].as_str().unwrap_or("").into(),
                    manifest: inv["manifest"].clone(),
                    to: keys.iter().filter(|k| st.friends.get(*k).map(|f| !f.pending).unwrap_or(false)).map(|k| (*k, "sent".to_string())).collect(),
                };
                for k in o.to.keys() {
                    if let Some(l) = st.links.get(k) {
                        let _ = l.tx.send(self.invite_msg(&o, &st));
                    }
                }
                // one active play-together session at a time
                let olds: Vec<OutInvite> = st.out_invites.drain(..).collect();
                for old in olds {
                    for k in old.to.keys() {
                        if let Some(l) = st.links.get(k) {
                            let _ = l.tx.send(PeerMsg::InviteCancel { id: old.id.clone() });
                        }
                    }
                }
                st.out_invites.push(o);
                drop(st);
                self.send_event(json!({"ev": "invite_sent", "id": id}));
            }
            "cancel_invite" => {
                let id = c["id"].as_str().unwrap_or("").to_string();
                let mut st = self.st.lock().unwrap();
                if let Some(pos) = st.out_invites.iter().position(|i| i.id == id || id.is_empty()) {
                    let inv = st.out_invites.remove(pos);
                    for k in inv.to.keys() {
                        if let Some(l) = st.links.get(k) {
                            let _ = l.tx.send(PeerMsg::InviteCancel { id: inv.id.clone() });
                        }
                    }
                }
            }
            "reply_invite" => {
                let from = key_arg("from")?;
                let id = c["id"].as_str().unwrap_or("").to_string();
                let accept = c["accept"].as_bool().unwrap_or(false);
                let exists = self.st.lock().unwrap().in_invites.iter().any(|i| i.from == from && i.id == id);
                if !exists {
                    bail!("no such invite");
                }
                self.send_to(&from, PeerMsg::InviteReply { id: id.clone(), accept });
                if accept {
                    {
                        let mut st = self.st.lock().unwrap();
                        if let Some(i) = st.in_invites.iter_mut().find(|i| i.from == from && i.id == id) {
                            if i.state == "accepted" {
                                return Ok(());
                            }
                            i.state = "accepted".into();
                        }
                    }
                    self.clone().start_session(from, id).await?;
                } else {
                    self.st.lock().unwrap().in_invites.retain(|i| !(i.from == from && i.id == id));
                }
            }
            "retry" => self.poll_now.notify_one(),
            _ => bail!("unknown command {cmd}"),
        }
        self.touch();
        Ok(())
    }
}

fn instance_name(st: &State, peer: &Key) -> String {
    st.in_invites.iter().rev().find(|i| &i.from == peer).map(|i| i.instance.clone()).unwrap_or_default()
}

fn verify_hello(conn: &Connection, key: &str, sig: &str, peer_is_client: bool) -> Result<Key> {
    let k = key_from_hex(key).ok_or_else(|| anyhow!("bad key"))?;
    let sig: [u8; 64] = data_encoding::HEXLOWER_PERMISSIVE
        .decode(sig.as_bytes())?
        .try_into()
        .map_err(|_| anyhow!("bad signature"))?;
    let mut exp = [0u8; 32];
    conn.export_keying_material(&mut exp, b"fashion-net", b"").map_err(|_| anyhow!("exporter"))?;
    VerifyingKey::from_bytes(&k)?.verify(&auth_message(&exp, peer_is_client), &Signature::from_bytes(&sig))?;
    Ok(k)
}

async fn write_msg(s: &mut SendStream, m: &PeerMsg) -> Result<()> {
    let mut line = serde_json::to_vec(m)?;
    line.push(b'\n');
    s.write_all(&line).await?;
    Ok(())
}

async fn read_msg(r: &mut BufReader<RecvStream>) -> Result<PeerMsg> {
    let mut line = Vec::new();
    loop {
        let buf = r.fill_buf().await?;
        if buf.is_empty() {
            bail!("stream ended");
        }
        if let Some(p) = buf.iter().position(|b| *b == b'\n') {
            line.extend_from_slice(&buf[..p]);
            r.consume(p + 1);
            break;
        }
        let n = buf.len();
        line.extend_from_slice(buf);
        r.consume(n);
        if line.len() > MAX_LINE {
            bail!("message too large");
        }
    }
    Ok(serde_json::from_slice(&line)?)
}

async fn writer(mut send: SendStream, mut rx: mpsc::UnboundedReceiver<PeerMsg>) {
    while let Some(m) = rx.recv().await {
        if write_msg(&mut send, &m).await.is_err() {
            break;
        }
    }
}

async fn pipe(tcp: tokio::net::TcpStream, mut send: SendStream, mut recv: BufReader<RecvStream>) {
    let (mut tr, mut tw) = tcp.into_split();
    let up = async {
        let _ = tokio::io::copy(&mut tr, &mut send).await;
        let _ = send.finish();
    };
    let down = async {
        let _ = tokio::io::copy_buf(&mut recv, &mut tw).await;
        let _ = tw.shutdown().await;
    };
    tokio::join!(up, down);
}
