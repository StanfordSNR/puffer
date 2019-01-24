import yaml

from helpers import create_time_clause, connect_to_influxdb, try_parsing_time
from ttp import prepare_raw_data
from plot_ssim_rebuffer import collect_rebuffer


VIDEO_DURATION = 180180
PKT_BYTES = 1500
MILLION = 1000000


# cache of Postgres data: experiment 'id' -> json 'data' of the experiment
expt_id_cache = {}


def collect_video_data(yaml_settings_path, time_start, time_end, cc):
    d = prepare_raw_data(yaml_settings_path, time_start, time_end, cc)

    for session in d:
        to_remove = []

        for video_ts in d[session]:
            dsv = d[session][video_ts]

            if 'trans_time' not in dsv or dsv['trans_time'] <= 0:
                to_remove.append(video_ts)
                continue

            dsv['size'] *= PKT_BYTES  # convert back to bytes
            dsv['delivery_rate'] *= PKT_BYTES  # convert back to byte/second

        for video_ts in to_remove:
            del d[session][video_ts]

    return d


def do_collect_buffer_data(client_buffer_results):
    d = {}

    excluded_sessions = {}
    last_ts = {}
    last_cum_rebuf = {}
    last_low_buf = {}

    for pt in client_buffer_results['client_buffer']:
        session = (pt['user'], int(pt['init_id']),
                   pt['channel'], int(pt['expt_id']))
        if session in excluded_sessions:
            continue

        if session not in d:
            d[session] = {}
            d[session]['min_play_time'] = None
            d[session]['max_play_time'] = None
            d[session]['min_cum_rebuf'] = None
            d[session]['max_cum_rebuf'] = None
        if session not in last_ts:
            last_ts[session] = None
        if session not in last_cum_rebuf:
            last_cum_rebuf[session] = None
        if session not in last_low_buf:
            last_low_buf[session] = None

        ts = try_parsing_time(pt['time'])
        buf = float(pt['buffer'])
        cum_rebuf = float(pt['cum_rebuf'])

        # verify that time is basically successive in the same session
        if last_ts[session] is not None:
            diff = (ts - last_ts[session]).total_seconds()
            if diff > 60:
                # a new different session should be ignored
                continue
        last_ts[session] = ts

        # identify outliers: exclude the sessions if there is a long low buffer
        if buf > 1:
            last_low_buf[session] = None
        else:
            if last_low_buf[session] is None:
                last_low_buf[session] = ts
            else:
                diff = (ts - last_low_buf[session]).total_seconds()
                if diff > 30:
                    print('Outlier session', session)
                    excluded_sessions[session] = True
                    continue

        # identify stalls caused by slow video decoding
        if last_cum_rebuf[session] is not None:
            if buf > 5 and cum_rebuf > last_cum_rebuf[session] + 0.25:
                # should not have stalls
                print('Decoding stalls', session)
                excluded_sessions[session] = True
                continue
        last_cum_rebuf[session] = cum_rebuf

        ds = d[session]  # short name
        if pt['event'] == 'startup':
            ds['min_play_time'] = ts
            ds['min_cum_rebuf'] = cum_rebuf

        if ds['max_play_time'] is None or ts > ds['max_play_time']:
            ds['max_play_time'] = ts

        if ds['max_cum_rebuf'] is None or cum_rebuf > ds['max_cum_rebuf']:
            ds['max_cum_rebuf'] = cum_rebuf

    ret = {}  # return value
    for session in d:
        if session in excluded_sessions:
            continue

        ds = d[session]
        if ds['min_play_time'] is None or ds['min_cum_rebuf'] is None:
            continue

        sess_play = (ds['max_play_time'] - ds['min_play_time']).total_seconds()
        # further exclude short sessions
        if sess_play < 5:
            continue

        if session not in ret:
            ret[session] = {}

        ret[session]['play'] = sess_play
        ret[session]['rebuf'] = ds['max_cum_rebuf'] - ds['min_cum_rebuf']
        ret[session]['startup'] = ds['min_cum_rebuf']

    return ret


def collect_buffer_data(yaml_settings_path, time_start, time_end):
    with open(yaml_settings_path, 'r') as fh:
        yaml_settings = yaml.safe_load(fh)

    # construct time clause after 'WHERE'
    time_clause = create_time_clause(time_start, time_end)

    # create an InfluxDB client and perform queries
    influx_client = connect_to_influxdb(yaml_settings)

    client_buffer_query = 'SELECT * FROM client_buffer'
    if time_clause is not None:
        client_buffer_query += ' WHERE ' + time_clause
    client_buffer_results = influx_client.query(client_buffer_query)
    if not client_buffer_results:
        sys.exit('Error: no results returned from query: ' + client_buffer_query)

    return do_collect_buffer_data(client_buffer_results)
