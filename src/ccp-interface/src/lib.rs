#[macro_use]
extern crate slog;
extern crate time;
extern crate portus;

use portus::{CongAlg, Config, Datapath, DatapathInfo, DatapathTrait, Report};
use portus::ipc::Ipc;
use portus::lang::{Bin, Scope};
use std::fmt;

pub struct Cinter<T: Ipc> {
    control_channel: Datapath<T>,
    logger: Option<slog::Logger>,
}

#[derive(Clone)]
pub struct CinterConfig {
}

impl Default for CinterConfig {
    fn default() -> Self {
        CinterConfig {}
    }
}

impl<T: Ipc> CongAlg<T> for Cinter<T> {
    type Config = CinterConfig;

    fn name() -> String {
        String::from("ccp_interface")
    }

    fn init_programs(_cfg: Config<T, Self>) -> Vec<(String, String)> {
        vec![
            (String::from("DatapathIntervalProg"), String::from("
                (def
                (Report
                    (volatile acked 0)
                    (volatile sacked 0)
                    (volatile loss 0)
                    (volatile timeout false)
                    (volatile rtt 0)
                    (volatile inflight 0)
                )
                (reportTime 0)
                )
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
                (when (> Micros reportTime)
                    (report)
                    (:= Micros 0)
                )
            ")),
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
            (String::from("AckUpdateProg"), String::from("
                (def (Report
                    (volatile acked 0)
                    (volatile sacked 0)
                    (volatile loss 0)
                    (volatile timeout false)
                    (volatile rtt 0)
                    (volatile inflight 0)
                ))
                (when true
                    (:= Report.acked (+ Report.acked Ack.bytes_acked))
                    (:= Report.sacked (+ Report.sacked Ack.packets_misordered))
                    (:= Report.loss Ack.lost_pkts_sample)
                    (:= Report.timeout Flow.was_timeout)
                    (:= Report.rtt Flow.rtt_sample_us)
                    (:= Report.inflight Flow.packets_in_flight)
                    (report)
                )
            ")),
            (String::from("SSUpdateProg"), String::from("
                (def (Report
                    (volatile acked 0)
                    (volatile sacked 0)
                    (volatile loss 0)
                    (volatile timeout false)
                    (volatile rtt 0)
                    (volatile inflight 0)
                ))
                (when true
                    (:= Report.acked (+ Report.acked Ack.bytes_acked))
                    (:= Report.sacked (+ Report.sacked Ack.packets_misordered))
                    (:= Report.loss Ack.lost_pkts_sample)
                    (:= Report.timeout Flow.was_timeout)
                    (:= Report.rtt Flow.rtt_sample_us)
                    (:= Report.inflight Flow.packets_in_flight)
                    (:= Cwnd (+ Cwnd Ack.bytes_acked))
                    (fallthrough)
                )
                (when (|| Report.timeout (> Report.loss 0))
                    (report)
                )

            "))]
    }

    fn create(control: Datapath<T>, cfg: Config<T, Cinter<T>>,
              info: DatapathInfo) -> Self {
        let mut s = Self {
            control_channel: control,
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
