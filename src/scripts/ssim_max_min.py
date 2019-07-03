#!/usr/bin/env python3

import os
import sys
import argparse
import yaml
from datetime import datetime, timedelta
import numpy as np
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt

from helpers import (
    connect_to_postgres, connect_to_influxdb, try_parsing_time,
    ssim_index_to_db, ssim_db_to_index, retrieve_expt_config)


def get_ssim_db(pt):
    if 'ssim_index' in pt and pt['ssim_index'] is not None:
        if float(pt['ssim_index']) == 1:
            return None
        else:
            return ssim_index_to_db(float(pt['ssim_index']))

    if 'ssim' in pt and pt['ssim'] is not None:
        return float(pt['ssim'])

    return None


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('yaml_settings')
    args = parser.parse_args()

    with open(args.yaml_settings, 'r') as fh:
        yaml_settings = yaml.safe_load(fh)

    influx_client = connect_to_influxdb(yaml_settings)
    ssim_results = influx_client.query('SELECT * FROM ssim')

    ssim_max = None
    ssim_min = None
    for pt in ssim_results['ssim']:
        ssim = get_ssim_db(pt)
        if ssim is None:
            continue

        if ssim_max is None or ssim > ssim_max:
            ssim_max = ssim

        if ssim_min is None or ssim < ssim_min:
            ssim_min = ssim

    print(ssim_max)
    print(ssim_min)


if __name__ == '__main__':
    main()
