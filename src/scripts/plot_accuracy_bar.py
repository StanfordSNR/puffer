#!/usr/bin/env python3

import sys
import argparse

import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt

import matplotlib.pyplot as plt
plt.rcParams['font.family'] = 'Times New Roman'
plt.rcParams['font.size'] = 14


args = None


def read_input_data(data_path):
    data = {}

    with open(data_path) as fh:
        for line in fh:
            if line.startswith('#'):
                continue

            model, bin_error, l1_loss, l2_loss = line.split(',')
            bin_error = float(bin_error)
            l1_loss = float(l1_loss)
            l2_loss = float(l2_loss)

            data[model] = {'bin_error': bin_error,
                           'l1_loss': l1_loss,
                           'l2_loss': l2_loss}

    return data


def plot_bar_graph(data):
    model_order = [
        'ttp', 'ttp_mle', 'ttp_throughput',
        'linear_regressor', 'harmonic_mean', 'tcp_delivery_rate',
        None,
        'ttp', 'ttp_no_history', 'ttp_no_history_delivery_rate',
        'ttp_no_history_rtt_minrtt', 'ttp_no_history_cwnd_inflight']


    model_color = {
        'ttp': 'C3',
        'ttp_mle': 'C0',
        'ttp_throughput': 'C1',
        'linear_regressor': 'C2',
        'harmonic_mean': 'C5',
        'tcp_delivery_rate': 'C7',
        'ttp_no_history': 'C0',
        'ttp_no_history_delivery_rate': 'C1',
        'ttp_no_history_rtt_minrtt': 'C2',
        'ttp_no_history_cwnd_inflight': 'C5',
    }

    model_label = {
        'ttp': 'TTP (Probabilistic)',
        'ttp_mle': 'TTP (Point Estimate)',
        'ttp_throughput': 'Throughput Predictor',
        'linear_regressor': 'Linear Regression (no DNN)',
        'harmonic_mean': 'Harmonic Mean (HM)',
        'tcp_delivery_rate': 'TCP Delivery Rate',
        'ttp_no_history': 'No History',
        'ttp_no_history_delivery_rate': 'No History + No Delivery Rate',
        'ttp_no_history_rtt_minrtt': 'No History + No RTT or min RTT',
        'ttp_no_history_cwnd_inflight': 'No History + No CWND or Packets in Flight',
    }

    fig, ax = plt.subplots()

    cnt = 0
    for model in model_order:
        cnt += 1
        if model is None:
            continue

        ax.barh(cnt, data[model]['l2_loss'], color=model_color[model])
        if model != 'tcp_delivery_rate':
            ax.text(data[model]['l2_loss'] * 1.1,
                    cnt + 0.2, model_label[model])
        else:
            ax.text(data[model]['l2_loss'] * 0.155,
                    cnt + 1.1, model_label[model])

    ax.set_xscale('log', basex=2)
    ax.xaxis.set_tick_params(which='both', top=True, labeltop=True)

    major_xticks = [0.125, 0.25, 0.5, 1, 2, 4, 8, 16, 32]
    ax.set_xticks(major_xticks, minor=False)
    ax.set_xticklabels(map(str, major_xticks), minor=False)

    ax.set_yticks([])
    ax.invert_yaxis()

    ax.tick_params(which='both', direction='in')
    ax.spines['right'].set_visible(False)

    ax.set_xlabel('Mean squared error of predicting chunk transmission time')

    fig.savefig(args.o, bbox_inches='tight')
    sys.stderr.write('Saved plot to {}\n'.format(args.o))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('-i', help='input data', required=True)
    parser.add_argument('-o', help='output figure', required=True)
    global args
    args = parser.parse_args()

    data = read_input_data(args.i)
    plot_bar_graph(data)


if __name__ == '__main__':
    main()
