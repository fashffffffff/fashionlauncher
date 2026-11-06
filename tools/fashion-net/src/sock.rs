// SPDX-License-Identifier: GPL-3.0-only
//! One UDP socket shared by QUIC and STUN: STUN responses are filtered out of QUIC's receive path,
//! so the public address we learn is exactly the one our QUIC traffic uses (needed for hole punching).

use crate::proto::{canon, parse_stun_response, stun_request};
use quinn::udp::{RecvMeta, Transmit};
use quinn::{AsyncUdpSocket, UdpPoller};
use std::collections::HashMap;
use std::fmt;
use std::io::{self, IoSliceMut};
use std::net::SocketAddr;
use std::pin::Pin;
use std::sync::{Arc, Mutex};
use std::task::{Context, Poll};
use std::time::Duration;
use tokio::sync::oneshot;

type Pending = Arc<Mutex<HashMap<[u8; 12], oneshot::Sender<SocketAddr>>>>;

pub struct DemuxSocket {
    inner: Arc<dyn AsyncUdpSocket>,
    pending: Pending,
}

impl fmt::Debug for DemuxSocket {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.write_str("DemuxSocket")
    }
}

/// Handle used to run STUN queries over the shared socket.
#[derive(Clone)]
pub struct Stun {
    inner: Arc<dyn AsyncUdpSocket>,
    pending: Pending,
    v6: bool,
}

impl DemuxSocket {
    pub fn wrap(inner: Arc<dyn AsyncUdpSocket>) -> (Arc<DemuxSocket>, Stun) {
        let pending: Pending = Default::default();
        let v6 = inner.local_addr().map(|a| a.is_ipv6()).unwrap_or(false);
        (
            Arc::new(DemuxSocket { inner: inner.clone(), pending: pending.clone() }),
            Stun { inner, pending, v6 },
        )
    }

    fn take_stun(&self, data: &[u8]) -> bool {
        if !crate::proto::is_stun(data) {
            return false;
        }
        if let Some((tid, addr)) = parse_stun_response(data) {
            if let Some(tx) = self.pending.lock().unwrap().remove(&tid) {
                let _ = tx.send(canon(addr));
            }
        }
        true
    }
}

impl AsyncUdpSocket for DemuxSocket {
    fn create_io_poller(self: Arc<Self>) -> Pin<Box<dyn UdpPoller>> {
        self.inner.clone().create_io_poller()
    }

    fn try_send(&self, transmit: &Transmit) -> io::Result<()> {
        self.inner.try_send(transmit)
    }

    fn poll_recv(
        &self,
        cx: &mut Context,
        bufs: &mut [IoSliceMut<'_>],
        meta: &mut [RecvMeta],
    ) -> Poll<io::Result<usize>> {
        loop {
            let n = match self.inner.poll_recv(cx, bufs, meta) {
                Poll::Ready(Ok(n)) => n,
                other => return other,
            };
            // compact: drop STUN datagrams, keep QUIC ones in order
            let mut kept = 0;
            for i in 0..n {
                let len = meta[i].len;
                if self.take_stun(&bufs[i][..len]) {
                    continue;
                }
                if kept != i {
                    let (a, b) = bufs.split_at_mut(i);
                    a[kept][..len].copy_from_slice(&b[0][..len]);
                    meta[kept] = meta[i];
                }
                kept += 1;
            }
            if kept > 0 || n == 0 {
                return Poll::Ready(Ok(kept));
            }
        }
    }

    fn local_addr(&self) -> io::Result<SocketAddr> {
        self.inner.local_addr()
    }
    fn max_transmit_segments(&self) -> usize {
        self.inner.max_transmit_segments()
    }
    fn max_receive_segments(&self) -> usize {
        self.inner.max_receive_segments()
    }
    fn may_fragment(&self) -> bool {
        self.inner.may_fragment()
    }
}

impl Stun {
    /// Asks one STUN server for our mapped address (3 tries, ~3 s total).
    pub async fn query(&self, server: SocketAddr) -> Option<SocketAddr> {
        if server.is_ipv6() && !self.v6 {
            return None;
        }
        let dest = match (server, self.v6) {
            (SocketAddr::V4(a), true) => SocketAddr::new(a.ip().to_ipv6_mapped().into(), a.port()),
            _ => server,
        };
        for _ in 0..3 {
            let tid: [u8; 12] = rand::random();
            let (tx, rx) = oneshot::channel();
            self.pending.lock().unwrap().insert(tid, tx);
            let req = stun_request(&tid);
            let _ = self.inner.try_send(&Transmit {
                destination: dest,
                ecn: None,
                contents: &req,
                segment_size: None,
                src_ip: None,
            });
            let res = tokio::time::timeout(Duration::from_millis(1000), rx).await;
            self.pending.lock().unwrap().remove(&tid);
            if let Ok(Ok(a)) = res {
                return Some(a);
            }
        }
        None
    }
}
