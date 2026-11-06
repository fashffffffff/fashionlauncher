// SPDX-License-Identifier: GPL-3.0-only
//! Pure protocol pieces: friend codes, presence records, peer messages, STUN, Minecraft LAN announcements.

use serde::{Deserialize, Serialize};
use serde_json::Value;
use std::net::{IpAddr, Ipv4Addr, Ipv6Addr, SocketAddr};

pub const ALPN: &[u8] = b"fashion-net/1";
pub const PROTO_VERSION: u32 = 1;
pub const LINK_PREFIX: &str = "fashion://friend/";
pub const LAN_GROUP: Ipv4Addr = Ipv4Addr::new(224, 0, 2, 60);
pub const LAN_PORT: u16 = 4445;
/// Appended to announcements we re-broadcast so our own listeners ignore them.
/// Minecraft ignores anything after `[/AD]`.
pub const LAN_TAG: &str = "[FN]1[/FN]";

// ---------- friend codes ----------

/// Friend code: the 32-byte public key in lowercase base32 (no padding), grouped by 4 with dashes.
pub fn encode_code(key: &[u8; 32]) -> String {
    let raw = data_encoding::BASE32_NOPAD.encode(key).to_lowercase();
    raw.as_bytes()
        .chunks(4)
        .map(|c| std::str::from_utf8(c).unwrap())
        .collect::<Vec<_>>()
        .join("-")
}

/// Accepts a code (any case, with or without dashes/spaces) or an invite link.
/// Returns the key and the display name carried by the link, if any.
pub fn parse_code(input: &str) -> Option<([u8; 32], Option<String>)> {
    let mut s = input.trim();
    let mut name = None;
    if let Some(pos) = s.find(LINK_PREFIX) {
        s = &s[pos + LINK_PREFIX.len()..];
        if let Some(q) = s.find('?') {
            let query = &s[q + 1..];
            for part in query.split('&') {
                if let Some(v) = part.strip_prefix("n=") {
                    let n = percent_decode(v);
                    if !n.trim().is_empty() {
                        name = Some(n.trim().chars().take(32).collect());
                    }
                }
            }
            s = &s[..q];
        }
    }
    let cleaned: String = s
        .chars()
        .filter(|c| c.is_ascii_alphanumeric())
        .map(|c| c.to_ascii_uppercase())
        .collect();
    let bytes = data_encoding::BASE32_NOPAD.decode(cleaned.as_bytes()).ok()?;
    let key: [u8; 32] = bytes.try_into().ok()?;
    Some((key, name))
}

pub fn make_link(key: &[u8; 32], name: &str) -> String {
    let code: String = encode_code(key).replace('-', "");
    if name.is_empty() {
        format!("{LINK_PREFIX}{code}")
    } else {
        format!("{LINK_PREFIX}{code}?n={}", percent_encode(name))
    }
}

fn percent_encode(s: &str) -> String {
    let mut out = String::new();
    for b in s.bytes() {
        if b.is_ascii_alphanumeric() || b"-_.~".contains(&b) {
            out.push(b as char);
        } else {
            out.push_str(&format!("%{b:02X}"));
        }
    }
    out
}

fn percent_decode(s: &str) -> String {
    let b = s.as_bytes();
    let mut out = Vec::new();
    let mut i = 0;
    while i < b.len() {
        if b[i] == b'%' && i + 2 < b.len() {
            if let Ok(v) = u8::from_str_radix(std::str::from_utf8(&b[i + 1..i + 3]).unwrap_or("zz"), 16) {
                out.push(v);
                i += 3;
                continue;
            }
        }
        out.push(if b[i] == b'+' { b' ' } else { b[i] });
        i += 1;
    }
    String::from_utf8_lossy(&out).into_owned()
}

pub fn key_hex(key: &[u8; 32]) -> String {
    data_encoding::HEXLOWER.encode(key)
}

pub fn key_from_hex(s: &str) -> Option<[u8; 32]> {
    data_encoding::HEXLOWER_PERMISSIVE.decode(s.as_bytes()).ok()?.try_into().ok()
}

/// DHT infohash where people who want to befriend `key` announce themselves.
pub fn inbox_hash(key: &[u8; 32]) -> [u8; 20] {
    let mut h = sha1_smol::Sha1::new();
    h.update(b"fashion-net inbox v1");
    h.update(key);
    h.digest().bytes()
}

// ---------- presence record (BEP 44 mutable item value, <= 1000 bytes) ----------

#[derive(Serialize, Deserialize, Debug, Clone, PartialEq, Default)]
pub struct Presence {
    #[serde(default)]
    pub v: u32,
    /// display name
    #[serde(default)]
    pub n: String,
    /// candidate endpoints "ip:port"
    #[serde(default)]
    pub e: Vec<String>,
    /// unix time (seconds) of publishing
    #[serde(default)]
    pub t: i64,
    /// "on" | "off"
    #[serde(default)]
    pub s: String,
    /// NAT kind: "open" | "easy" | "hard" | "unknown"
    #[serde(default)]
    pub x: String,
}

impl Presence {
    pub fn encode(&self) -> Vec<u8> {
        let mut p = self.clone();
        p.n = p.n.chars().take(32).collect();
        loop {
            let v = serde_json::to_vec(&p).unwrap();
            if v.len() <= 1000 || p.e.is_empty() {
                return v;
            }
            p.e.pop();
        }
    }
    pub fn decode(b: &[u8]) -> Option<Presence> {
        serde_json::from_slice(b).ok()
    }
    pub fn endpoints(&self) -> Vec<SocketAddr> {
        self.e.iter().filter_map(|s| s.parse().ok()).collect()
    }
    /// Online if published recently and not explicitly signed off.
    pub fn is_fresh(&self, now: i64) -> bool {
        self.s != "off" && now - self.t < 6 * 60 && self.t - now < 120
    }
}

// ---------- peer messages (JSON lines over the QUIC control stream) ----------

#[derive(Serialize, Deserialize, Debug, Clone, PartialEq)]
#[serde(tag = "t", rename_all = "snake_case")]
pub enum PeerMsg {
    Hello { ver: u32, key: String, name: String, sig: String },
    /// Sender wants to be friends.
    Add,
    Accepted,
    Declined,
    Unfriend,
    Name { name: String },
    Presence { status: String, #[serde(default)] game: String },
    Invite {
        id: String,
        #[serde(default)]
        instance: String,
        #[serde(default)]
        mc: String,
        #[serde(default)]
        manifest: Value,
        #[serde(default)]
        world: Option<World>,
    },
    InviteCancel { id: String },
    InviteReply { id: String, accept: bool },
    /// Host's LAN world state for an invite (None = not open yet).
    World { id: String, world: Option<World> },
    /// First line of a tunnel stream.
    Tcp { id: String },
}

#[derive(Serialize, Deserialize, Debug, Clone, PartialEq)]
pub struct World {
    pub port: u16,
    pub motd: String,
}

/// Bytes each side signs to prove its identity, bound to the TLS session via an exporter.
pub fn auth_message(exporter: &[u8; 32], is_client: bool) -> Vec<u8> {
    let mut m = b"fashion-net auth v1".to_vec();
    m.push(if is_client { b'c' } else { b's' });
    m.extend_from_slice(exporter);
    m
}

// ---------- STUN (RFC 5389 binding) ----------

pub const STUN_MAGIC: [u8; 4] = [0x21, 0x12, 0xA4, 0x42];

pub fn stun_request(tid: &[u8; 12]) -> [u8; 20] {
    let mut p = [0u8; 20];
    p[0..2].copy_from_slice(&0x0001u16.to_be_bytes());
    p[4..8].copy_from_slice(&STUN_MAGIC);
    p[8..20].copy_from_slice(tid);
    p
}

pub fn is_stun(p: &[u8]) -> bool {
    p.len() >= 20 && p[0] & 0xC0 == 0 && p[4..8] == STUN_MAGIC
}

/// Parses a binding success response; returns transaction id and mapped address.
pub fn parse_stun_response(p: &[u8]) -> Option<([u8; 12], SocketAddr)> {
    if !is_stun(p) || u16::from_be_bytes([p[0], p[1]]) != 0x0101 {
        return None;
    }
    let tid: [u8; 12] = p[8..20].try_into().ok()?;
    let len = u16::from_be_bytes([p[2], p[3]]) as usize;
    let body = p.get(20..20 + len)?;
    let mut i = 0;
    let mut mapped = None;
    while i + 4 <= body.len() {
        let ty = u16::from_be_bytes([body[i], body[i + 1]]);
        let l = u16::from_be_bytes([body[i + 2], body[i + 3]]) as usize;
        let v = body.get(i + 4..i + 4 + l)?;
        if ty == 0x0020 || ty == 0x0001 {
            let xor = ty == 0x0020;
            if v.len() >= 8 {
                let mut port = u16::from_be_bytes([v[2], v[3]]);
                if xor {
                    port ^= 0x2112;
                }
                let addr = match v[1] {
                    1 => {
                        let mut ip: [u8; 4] = v[4..8].try_into().ok()?;
                        if xor {
                            for k in 0..4 {
                                ip[k] ^= STUN_MAGIC[k];
                            }
                        }
                        Some(SocketAddr::new(IpAddr::V4(Ipv4Addr::from(ip)), port))
                    }
                    2 if v.len() >= 20 => {
                        let mut ip: [u8; 16] = v[4..20].try_into().ok()?;
                        if xor {
                            let mut mask = [0u8; 16];
                            mask[..4].copy_from_slice(&STUN_MAGIC);
                            mask[4..].copy_from_slice(&tid);
                            for k in 0..16 {
                                ip[k] ^= mask[k];
                            }
                        }
                        Some(SocketAddr::new(IpAddr::V6(Ipv6Addr::from(ip)), port))
                    }
                    _ => None,
                };
                if addr.is_some() && (xor || mapped.is_none()) {
                    mapped = addr;
                }
            }
        }
        i += 4 + ((l + 3) & !3);
    }
    Some((tid, mapped?))
}

// ---------- Minecraft LAN announcements ----------

pub fn parse_lan(msg: &str) -> Option<World> {
    if msg.contains(LAN_TAG) {
        return None;
    }
    let ms = msg.find("[MOTD]")? + 6;
    let me = msg[ms..].find("[/MOTD]")? + ms;
    let rest = &msg[me + 7..];
    let a = rest.find("[AD]")? + 4;
    let b = rest[a..].find("[/AD]")? + a;
    let port: u16 = rest[a..b].trim().parse().ok()?;
    if port == 0 {
        return None;
    }
    Some(World { port, motd: msg[ms..me].to_string() })
}

pub fn format_lan(motd: &str, port: u16) -> String {
    format!("[MOTD]{}[/MOTD][AD]{}[/AD]{}", motd.replace('[', "("), port, LAN_TAG)
}

/// Normalise v4-mapped v6 addresses back to v4.
pub fn canon(a: SocketAddr) -> SocketAddr {
    SocketAddr::new(a.ip().to_canonical(), a.port())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn code_roundtrip() {
        let key = [7u8; 32];
        let code = encode_code(&key);
        assert!(code.contains('-'));
        assert_eq!(parse_code(&code).unwrap().0, key);
        assert_eq!(parse_code(&code.to_uppercase().replace('-', " ")).unwrap().0, key);
        let link = make_link(&key, "Steve & Alex");
        let (k, n) = parse_code(&link).unwrap();
        assert_eq!(k, key);
        assert_eq!(n.as_deref(), Some("Steve & Alex"));
        assert!(parse_code("not a code").is_none());
        assert!(parse_code("").is_none());
    }

    #[test]
    fn presence_fits_and_freshness() {
        let p = Presence {
            v: 1,
            n: "x".repeat(100),
            e: (0..100).map(|i| format!("[2001:db8::{i:x}]:65535")).collect(),
            t: 1000,
            s: "on".into(),
            x: "easy".into(),
        };
        let enc = p.encode();
        assert!(enc.len() <= 1000);
        let d = Presence::decode(&enc).unwrap();
        assert_eq!(d.n.chars().count(), 32);
        assert!(d.is_fresh(1100));
        assert!(!d.is_fresh(1000 + 7 * 60));
        assert_eq!(d.endpoints().len(), d.e.len());
    }

    #[test]
    fn peer_msg_json() {
        let m = PeerMsg::Invite {
            id: "a1".into(),
            instance: "Pack".into(),
            mc: "1.21.4".into(),
            manifest: serde_json::json!({"mods": []}),
            world: Some(World { port: 5555, motd: "W".into() }),
        };
        let s = serde_json::to_string(&m).unwrap();
        assert!(s.contains("\"t\":\"invite\""));
        assert_eq!(serde_json::from_str::<PeerMsg>(&s).unwrap(), m);
        let a: PeerMsg = serde_json::from_str(r#"{"t":"add"}"#).unwrap();
        assert_eq!(a, PeerMsg::Add);
        let p: PeerMsg = serde_json::from_str(r#"{"t":"presence","status":"online"}"#).unwrap();
        assert_eq!(p, PeerMsg::Presence { status: "online".into(), game: String::new() });
    }

    #[test]
    fn stun_parse() {
        let tid = [9u8; 12];
        assert!(is_stun(&stun_request(&tid)));
        // build XOR-MAPPED-ADDRESS response for 203.0.113.5:40000
        let mut r = vec![0x01, 0x01, 0, 12];
        r.extend_from_slice(&STUN_MAGIC);
        r.extend_from_slice(&tid);
        r.extend_from_slice(&[0x00, 0x20, 0, 8, 0, 1]);
        r.extend_from_slice(&(40000u16 ^ 0x2112).to_be_bytes());
        let ip = [203u8, 0, 113, 5];
        for k in 0..4 {
            r.push(ip[k] ^ STUN_MAGIC[k]);
        }
        let (t, a) = parse_stun_response(&r).unwrap();
        assert_eq!(t, tid);
        assert_eq!(a, "203.0.113.5:40000".parse().unwrap());
        // QUIC long header packet is not STUN
        let mut q = r.clone();
        q[0] = 0xC3;
        assert!(!is_stun(&q));
    }

    #[test]
    fn lan_parse() {
        let w = parse_lan("[MOTD]Steve - New World[/MOTD][AD]51234[/AD]").unwrap();
        assert_eq!(w, World { port: 51234, motd: "Steve - New World".into() });
        assert!(parse_lan("[MOTD]x[/MOTD]").is_none());
        assert!(parse_lan(&format_lan("x", 1)).is_none(), "own re-broadcasts are ignored");
        let f = format_lan("A [b]", 25565);
        assert!(f.starts_with("[MOTD]A (b][/MOTD][AD]25565[/AD]"));
    }

    #[test]
    fn inbox_hash_stable() {
        assert_eq!(inbox_hash(&[1; 32]), inbox_hash(&[1; 32]));
        assert_ne!(inbox_hash(&[1; 32]), inbox_hash(&[2; 32]));
    }
}
