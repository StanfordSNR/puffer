#[macro_use]
extern crate slog;
extern crate portus;
extern crate time;

use portus::ipc::Ipc;
use portus::lang::{Bin, Scope};
use portus::{Config, CongAlg, Datapath, DatapathInfo, DatapathTrait, Report};
use std::fmt;

pub struct Puffer<T: Ipc> {
    control_channel: Datapath<T>,
    logger: Option<slog::Logger>,
}

#[derive(Clone)]
pub struct PufferConfig {}

impl Default for PufferConfig {
    fn default() -> Self {
        PufferConfig {}
    }
}

impl<T: Ipc> CongAlg<T> for Puffer<T> {
    type Config = PufferConfig;

    fn name() -> String {
        String::from("CCP Interface for Puffer")
    }

    fn init_programs(_cfg: Config<T, Self>) -> Vec<(String, String)> {
        vec![
            (String::from("DatapathIntervalRTTProg"), String::from("
                (def (Report
                    (volatile acked 0)
                    (volatile sacked 0)
                    (volatile loss 0)
                    (volatile timeout false)
                    (volatile rtt 0)
                    (volatile inflight 0)
                ))
                (when true
                    (:= Report.inflight Flow.packets_in_flight)
                    (:= Report.rtt Flow.rtt_sample_us)
                    (:= Report.acked (+ Report.acked Ack.bytes_acked))
                    (:= Report.sacked (+ Report.sacked Ack.packets_misordered))
                    (:= Report.loss Ack.lost_pkts_sample)
                    (:= Report.timeout Flow.was_timeout)
                    (fallthrough)
                )
                (when (|| Report.timeout (> Report.loss 0))
                    (report)
                    (:= Micros 0)
                )
                (when (> Micros Flow.rtt_sample_us)
                    (report)
                    (:= Micros 0)
                )
            ")),
        ]
    }

    fn create(control_channel: Datapath<T>, cfg: Config<T, Puffer<T>>, info: DatapathInfo) -> Self {
        let mut s = Self {
            control_channel: control_channel,
            logger: cfg.logger,
        };
        s
    }

    fn on_report(&mut self, _sock_id: u32, m: Report) {
        self.logger.as_ref().map(|log| {
            debug!(log, "on_report";
            );
        });
    }
}
