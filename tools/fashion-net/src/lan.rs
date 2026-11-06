// SPDX-License-Identifier: GPL-3.0-only
//! Minecraft "Open to LAN" multicast: listening for the host's world and re-announcing tunnels.

use crate::proto::{LAN_GROUP, LAN_PORT};
use socket2::{Domain, Protocol, Socket, Type};
use std::net::{IpAddr, Ipv4Addr, SocketAddr, SocketAddrV4};

/// Shares port 4445 with Minecraft's own LAN scanner (SO_REUSEADDR/SO_REUSEPORT).
pub fn listener(local_v4: &[Ipv4Addr]) -> std::io::Result<tokio::net::UdpSocket> {
    let s = Socket::new(Domain::IPV4, Type::DGRAM, Some(Protocol::UDP))?;
    s.set_reuse_address(true)?;
    #[cfg(unix)]
    s.set_reuse_port(true)?;
    s.bind(&SocketAddr::V4(SocketAddrV4::new(Ipv4Addr::UNSPECIFIED, LAN_PORT)).into())?;
    let mut joined = s.join_multicast_v4(&LAN_GROUP, &Ipv4Addr::UNSPECIFIED).is_ok();
    for ip in local_v4.iter().chain(std::iter::once(&Ipv4Addr::LOCALHOST)) {
        joined |= s.join_multicast_v4(&LAN_GROUP, ip).is_ok();
    }
    if !joined {
        return Err(std::io::Error::other("could not join the LAN multicast group"));
    }
    s.set_nonblocking(true)?;
    tokio::net::UdpSocket::from_std(s.into())
}

/// Sender for announcements; multicast loopback on so a game on this computer sees them.
pub fn sender() -> std::io::Result<tokio::net::UdpSocket> {
    let s = Socket::new(Domain::IPV4, Type::DGRAM, Some(Protocol::UDP))?;
    s.set_multicast_loop_v4(true)?;
    s.set_multicast_ttl_v4(1)?;
    s.bind(&SocketAddr::V4(SocketAddrV4::new(Ipv4Addr::UNSPECIFIED, 0)).into())?;
    s.set_nonblocking(true)?;
    tokio::net::UdpSocket::from_std(s.into())
}

pub async fn announce(sock: &tokio::net::UdpSocket, msg: &str) {
    let dest = SocketAddr::V4(SocketAddrV4::new(LAN_GROUP, LAN_PORT));
    if sock.send_to(msg.as_bytes(), dest).await.is_err() {
        // no default multicast route (offline): use loopback
        let s = socket2::SockRef::from(sock);
        let _ = s.set_multicast_if_v4(&Ipv4Addr::LOCALHOST);
        let _ = sock.send_to(msg.as_bytes(), dest).await;
    }
}

/// Addresses of this computer (non-loopback v4 for LAN candidates, global v6).
pub fn local_addrs() -> (Vec<Ipv4Addr>, Vec<std::net::Ipv6Addr>) {
    let mut v4 = Vec::new();
    let mut v6 = Vec::new();
    for i in if_addrs::get_if_addrs().unwrap_or_default() {
        if i.is_loopback() {
            continue;
        }
        match i.ip() {
            IpAddr::V4(a) if !a.is_link_local() && !a.is_unspecified() => v4.push(a),
            IpAddr::V6(a) => {
                let s = a.segments()[0];
                // global unicast 2000::/3 only (no link-local, no ULA)
                if (s & 0xe000) == 0x2000 {
                    v6.push(a)
                }
            }
            _ => {}
        }
    }
    v4.sort();
    v4.dedup();
    v6.sort();
    v6.dedup();
    (v4, v6)
}

pub fn is_local_ip(ip: IpAddr, v4: &[Ipv4Addr], v6: &[std::net::Ipv6Addr]) -> bool {
    let ip = ip.to_canonical();
    ip.is_loopback()
        || match ip {
            IpAddr::V4(a) => v4.contains(&a),
            IpAddr::V6(a) => v6.contains(&a),
        }
}
