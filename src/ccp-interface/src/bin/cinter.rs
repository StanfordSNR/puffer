extern crate clap;
use clap::Arg;
extern crate time;
#[macro_use]
extern crate slog;

extern crate ccp_interface;
extern crate portus;

use ccp_interface::Cinter;
use portus::ipc::{BackendBuilder, Blocking};

fn make_args() -> Result<(ccp_interface::CinterConfig, String), String> {
    let matches = clap::App::new("CCP INTERFACE")
        .about("Implementation of ccp interface for puffer")
        .arg(Arg::with_name("ipc")
             .long("ipc")
             .help("Sets the type of ipc to use: (netlink|unix)")
             .default_value("netlink")
             .validator(portus::algs::ipc_valid))
        .get_matches();

    Ok((
        ccp_interface::CinterConfig {},
        String::from(matches.value_of("ipc").unwrap()),
    ))
}

fn main() {
    let log = portus::algs::make_logger();
    let (cfg, ipc) = make_args()
        .map_err(|e| warn!(log, "bad argument"; "err" => ?e))
        .unwrap_or_default();

    info!(log, "starting CCP";
        "algorithm" => "ccp_interface",
        "ipc" => ipc.clone(),
    );
    match ipc.as_str() {
        "unix" => {
            use portus::ipc::unix::Socket;
            let b = Socket::<Blocking>::new("in", "out")
                .map(|sk| BackendBuilder{sock: sk})
                .expect("ipc initialization");
            portus::run::<_, Cinter<_>>(
                b,
                &portus::Config {
                    logger: Some(log),
                    config: cfg,
                }
            ).unwrap();
        }
        #[cfg(all(target_os = "linux"))]
        "netlink" => {
            use portus::ipc::netlink::Socket;
            let b = Socket::<Blocking>::new()
                .map(|sk| BackendBuilder{sock: sk})
                .expect("ipc initialization");
            portus::run::<_, Cinter<_>>(
                b,
                &portus::Config {
                    logger: Some(log),
                    config: cfg,
                }
            ).unwrap();
        }
        _ => unreachable!(),
    }
}
