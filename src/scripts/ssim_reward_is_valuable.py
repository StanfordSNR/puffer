#!/usr/bin/env python3

import sys
import os
import yaml
import numpy as np
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from helpers import (
    connect_to_influxdb, connect_to_postgres, query_measurement,
    retrieve_expt_config, create_time_clause, ssim_index_to_db)
from collect_data import video_data_by_session, VIDEO_DURATION


# The purpose of this script is to create a figure that demonstrates
# why including SSIM in the reward metric is valuable. It is predicated
# on the assumption that SSIM maps better to visual quality than bitrate
# does, which is a topic well addressed in prior work.

# Specifically, the figure this script produces shows that optimizing
# for SSIM in your reward function can lead to different decisions than
# optimizing for 'average bitrate' of the different qualities available
# in the video, and that these different decisions lead to better performance
# as evalauted by average SSIM experienced by the end user, which we
# have already noted is a more valuable evaluation metric than average bitrate
# streamed to the user.

def query_with_channel(influx_client, measurement, time_start, time_end, channel):
    time_clause = create_time_clause(time_start, time_end)

    query = 'SELECT * FROM ' + measurement
    if time_clause is not None:
        query += ' WHERE ' + time_clause + ' AND "channel"=\'nbc\' '

    results = influx_client.query(query)
    if not results:
        sys.exit('Error: no results returned from query: ' + query)

    return results



def plot(data, output):
    fig, ax = plt.subplots()
    title = 'SSIM of 3 consecutive chunks played on NBC (2019-01-06)'
    ax.set_title(title)
    ax.set_xlabel('Chunk Number')
    ax.set_ylabel('Average SSIM (dB)')
    ax.grid()

    quality_dict = {}
    for index, chunk in enumerate(data):
        for q_and_s in chunk:
            ssim_db = ssim_index_to_db(q_and_s[1])
            #ax.scatter(index, ssim_db)
            if q_and_s[0] not in quality_dict:
                quality_dict[q_and_s[0]] = {}
                quality_dict[q_and_s[0]]['x'] = []
                quality_dict[q_and_s[0]]['x'].append(index)
                quality_dict[q_and_s[0]]['y'] = []
                quality_dict[q_and_s[0]]['y'].append(ssim_db)
            else:
                quality_dict[q_and_s[0]]['x'].append(index)
                quality_dict[q_and_s[0]]['y'].append(ssim_db)

            if index == 0:
                if q_and_s[0] == '426x240-26':
                    #ax.annotate('189 kbps', (index, ssim_db))
                    pass
                elif q_and_s[0] == '1920x1080-20':
                    #ax.annotate('5540 kbps', (index, ssim_db))
                    pass
    for key, item in quality_dict.items():
        if key == '426x240-26':
            ax.plot(item['x'], item['y'], '-o', label='200 kbps')
        elif key == '1920x1080-20':
            ax.plot(item['x'], item['y'], '-o', label='5500 kbps')
        else:
            ax.plot(item['x'], item['y'], '-o')
    xmin, xmax = ax.get_xlim()
    #xmin = max(xmin, 0)
    #xmax = min(xmax, 100)
    ax.set_xlim(xmin, xmax)
    ax.legend(loc='lower left')
    fig.savefig(output, dpi=200, bbox_inches='tight')
    sys.stderr.write('Saved plot to {}\n'.format(output))


def main():
    # create an InfluxDB client and perform queries
    yaml_settings_str = '../settings.yml'
    with open(yaml_settings_str, 'r') as fh:
        yaml_settings = yaml.safe_load(fh)

    influx_client = connect_to_influxdb(yaml_settings)

    # query data from video_sent and video_acked
    for cnt in range(8,9):
        ssim_results = query_with_channel(influx_client, 'ssim',
                '2019-01-' + str(14-cnt).zfill(2) + 'T00:00:00Z', '2019-01-' + str(14-cnt).zfill(2) + 'T23:59:00Z', 'nbc')
        results = []
        all_results = []
        for pt in ssim_results['ssim']:
            all_results.append((pt['format'], pt['ssim_index'], pt['timestamp']))
            if pt['format'] != '426x240-26':
                continue
            results.append((pt['format'], pt['ssim_index'], pt['timestamp']))
        results.sort(key=lambda x: x[2])
        one_ago_ssim = 0
        two_ago_ssim = 0
        max_change = 0
        max_ts = 0
        max_idx = 0
        last_ts = 0
        for index, item in enumerate(results):
            if one_ago_ssim == 0:
                # first element
                one_ago_ssim = item[1]
                last_ts = item[2]
                continue
            if item[2] != last_ts + 180180:
                print("TIMESTAMP ERROR")
                one_ago_ssim = 0
                two_ago_ssim = 0
            last_ts = item[2]
            if two_ago_ssim == 0:
                two_ago_ssim = one_ago_ssim
                continue;
            
            dif1 = two_ago_ssim - one_ago_ssim
            dif2 = item[1] - one_ago_ssim
            two_ago_ssim = one_ago_ssim
            one_ago_ssim = item[1]

            total_change = abs(dif1) + abs(dif2)
            if total_change > max_change and abs(dif1) > 0.08 and abs(dif2) > 0.08:
                print(abs(dif1))
                max_change = total_change
                max_ts = item[2]
                max_idx = index
        
        print(str(results[max_idx]))
        print(str(results[max_idx - 1]))
        print(str(results[max_idx - 2]))
        ssim_and_quality_0 = [] 
        ssim_and_quality_1 = [] 
        ssim_and_quality_2 = [] 
        for item in all_results:
            if item[2] == results[max_idx][2]:
                ssim_and_quality_2.append(item)

        for item in all_results:
            if item[2] == results[max_idx - 1][2]:
                ssim_and_quality_1.append(item)
        
        for item in all_results:
            if item[2] == results[max_idx - 2][2]:
                ssim_and_quality_0.append(item)

        data = []
        data.append(ssim_and_quality_0)
        data.append(ssim_and_quality_1)
        data.append(ssim_and_quality_2)
        output_file = 'better_than_bitrate' + str(cnt) + '.png'
        plot(data, output_file)


if __name__ == '__main__':
    main()

