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
    scope: Scope,
    mss: u32,
    init_cwnd: f64,
    cwnd: f64,
}

#[derive(Clone)]
pub struct PufferConfig {}

impl Default for PufferConfig {
    fn default() -> Self {
        PufferConfig {}
    }
}

pub struct CongMeasurements {
	pub rtt: u32,
	pub acked: u32,
	pub sacked: u32,
	pub inflight: u32,
	pub loss: u32,
	pub was_timeout: bool,
}

impl<T: Ipc> Puffer<T> {
    fn get_fields(&self, m: &Report) -> CongMeasurements {
        let sc = &self.scope;

        let rtt = m.get_field(&String::from("Report.rtt"), sc).expect(
            "expected rtt field in returned measurement",
        ) as u32;

        let acked = m.get_field(&String::from("Report.acked"), sc).expect(
            "expected acked field in returned measurement",
        ) as u32;

        let sacked = m.get_field(&String::from("Report.sacked"), sc).expect(
            "expected sacked field in returned measurement",
        ) as u32;

        let inflight = m.get_field(&String::from("Report.inflight"), sc).expect(
            "expected inflight field in returned measurement",
        ) as u32;

        let loss = m.get_field(&String::from("Report.loss"), sc).expect(
            "expected loss field in returned measurement",
        ) as u32;

        let timeout = m.get_field(&String::from("Report.timeout"), sc).expect(
            "expected timeout field in returned measurement",
        ) as u32;

        CongMeasurements {
            rtt,
            acked,
            sacked,
            inflight,
            loss,
            was_timeout: timeout == 1,
        }
    }

    fn update_cwnd(&self) {
        if let Err(e) = self.control_channel
            .update_field(&self.scope, &[("Cwnd", self.cwnd as u32)]) {
            self.logger.as_ref().map(|log| {
                warn!(log, "Cwnd update error";
                      "err" => ?e,
                );
            });
        }
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
                    (volatile rtt 0)
                    (volatile acked 0)
                    (volatile sacked 0)
                    (volatile inflight 0)
                    (volatile loss 0)
                    (volatile timeout false)
                ))
                (when true
                    (:= Report.rtt Flow.rtt_sample_us)
                    (:= Report.acked (+ Report.acked Ack.bytes_acked))
                    (:= Report.sacked (+ Report.sacked Ack.packets_misordered))
                    (:= Report.inflight Flow.packets_in_flight)
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

    fn create(control_channel: Datapath<T>, cfg: Config<T, Puffer<T>>,
              info: DatapathInfo) -> Self {
        let mut s = Self {
            control_channel: control_channel,
            logger: cfg.logger,
            scope: Scope::new(),
            mss: info.mss,
            init_cwnd: info.init_cwnd as f64,
            cwnd: info.init_cwnd as f64,
        };

        s.scope = s.control_channel.set_program(
            String::from("DatapathIntervalRTTProg"), None).unwrap();

        s
    }

    fn on_report(&mut self, _sock_id: u32, m: Report) {
        let ms = self.get_fields(&m);

        if ms.loss == 0 {
            // increase cwnd by 1/cwnd per packet
            self.cwnd += self.mss as f64 * ms.acked as f64 / self.cwnd;
        } else {
            // until loss detected
            self.cwnd /= 2.0;
            if self.cwnd <= self.init_cwnd {
                self.cwnd = self.init_cwnd;
            }
        }

        // update cwnd used in datapath to self.cwnd
        self.update_cwnd();

        self.logger.as_ref().map(|log| {
            debug!(log, "on_report";
                "rtt" => ms.rtt,
                "acked" => ms.acked,
                "sacked" => ms.sacked,
                "inflight" => ms.inflight,
                "loss" => ms.loss,
                "was_timeout" => ms.was_timeout,
            );
        });
    }
}
