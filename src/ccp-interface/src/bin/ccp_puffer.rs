extern crate clap;
use clap::Arg;
extern crate time;
#[macro_use]
extern crate slog;

extern crate ccp_interface;
extern crate portus;

use ccp_interface::Puffer;
use portus::ipc::{BackendBuilder, Blocking};

fn make_args() -> Result<(ccp_interface::PufferConfig, String), String> {
    let matches = clap::App::new("CCP INTERFACE")
        .about("Implementation of CCP interface for Puffer")
        .arg(
            Arg::with_name("ipc")
                .long("ipc")
                .help("Sets the type of ipc to use: (netlink|unix)")
                .default_value("netlink")
                .validator(portus::algs::ipc_valid),
        )
        .get_matches();

    Ok((
        ccp_interface::PufferConfig {},
        String::from(matches.value_of("ipc").unwrap()),
    ))
}

fn main() {
    let log = portus::algs::make_logger();
    let (cfg, ipc) = make_args()
        .map_err(|e| warn!(log, "bad argument"; "err" => ?e))
        .unwrap_or_default();

    info!(log, "starting CCP";
        "algorithm" => "Puffer",
        "ipc" => ipc.clone(),
    );
    match ipc.as_str() {
        "unix" => {
            use portus::ipc::unix::Socket;
            let b = Socket::<Blocking>::new("in", "out")
                .map(|sk| BackendBuilder { sock: sk })
                .expect("ipc initialization");
            portus::run::<_, Puffer<_>>(
                b,
                &portus::Config {
                    logger: Some(log),
                    config: cfg,
                },
            )
            .unwrap();
        }
        #[cfg(all(target_os = "linux"))]
        "netlink" => {
            use portus::ipc::netlink::Socket;
            let b = Socket::<Blocking>::new()
                .map(|sk| BackendBuilder { sock: sk })
                .expect("ipc initialization");
            portus::run::<_, Puffer<_>>(
                b,
                &portus::Config {
                    logger: Some(log),
                    config: cfg,
                },
            )
            .unwrap();
        }
        _ => unreachable!(),
    }
}
