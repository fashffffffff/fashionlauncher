// SPDX-License-Identifier: GPL-3.0-only
//! fashion-net: serverless friends and play-together sidecar for Fashion Launcher.
//! Talks JSON lines with the launcher over stdin (commands) / stdout (events); logs go to stderr.

mod lan;
mod node;
mod proto;
mod sock;
mod tls;

use std::path::PathBuf;
use tokio::io::{AsyncBufReadExt, AsyncWriteExt};

fn arg(args: &[String], name: &str) -> Option<String> {
    args.iter().position(|a| a == name).and_then(|i| args.get(i + 1).cloned())
}

#[tokio::main]
async fn main() -> anyhow::Result<()> {
    let args: Vec<String> = std::env::args().collect();

    // Test helper: run a private DHT and print its bootstrap addresses.
    if let Some(n) = arg(&args, "--dht-testnet") {
        let n: usize = n.parse().unwrap_or(5);
        let net = tokio::task::spawn_blocking(move || mainline::Testnet::builder(n).build()).await??;
        println!("{}", net.bootstrap.join(","));
        net.leak();
        std::future::pending::<()>().await;
    }

    let data = PathBuf::from(arg(&args, "--data").ok_or_else(|| anyhow::anyhow!("--data <dir> is required"))?);
    let opts = node::Options {
        data,
        name: arg(&args, "--name").unwrap_or_default(),
        bootstrap: arg(&args, "--bootstrap")
            .map(|s| s.split(',').filter(|x| !x.is_empty()).map(String::from).collect())
            .unwrap_or_default(),
        port: arg(&args, "--port").and_then(|p| p.parse().ok()).unwrap_or(0),
        loopback: args.iter().any(|a| a == "--loopback") || std::env::var_os("FASHION_NET_LOOPBACK").is_some(),
        no_stun: args.iter().any(|a| a == "--no-stun"),
    };

    let (tx, mut rx) = tokio::sync::mpsc::unbounded_channel::<String>();
    tokio::spawn(async move {
        let mut out = tokio::io::stdout();
        while let Some(line) = rx.recv().await {
            if out.write_all(format!("{line}\n").as_bytes()).await.is_err() || out.flush().await.is_err() {
                std::process::exit(0);
            }
        }
    });

    let node = node::Node::start(opts, tx).await?;
    let mut lines = tokio::io::BufReader::new(tokio::io::stdin()).lines();
    while let Ok(Some(line)) = lines.next_line().await {
        let Ok(cmd) = serde_json::from_str::<serde_json::Value>(&line) else { continue };
        if cmd["cmd"] == "quit" {
            break;
        }
        if let Err(e) = node.command(cmd).await {
            eprintln!("[cmd] {e:#}");
        }
    }
    node.sign_off().await;
    std::process::exit(0);
}
