import sys
from helpers import try_parsing_time, get_ssim_index


VIDEO_DURATION = 180180
PKT_BYTES = 1500
MILLION = 1000000


def video_data_by_session(video_sent_results, video_acked_results):
    d = {}
    last_video_ts = {}

    for pt in video_sent_results['video_sent']:
        session = (pt['user'], int(pt['init_id']), pt['expt_id'])
        if session not in d:
            d[session] = {}
            last_video_ts[session] = None

        video_ts = int(pt['video_ts'])
        if last_video_ts[session] is not None:
            if video_ts != last_video_ts[session] + VIDEO_DURATION:
                continue
        last_video_ts[session] = video_ts

        d[session][video_ts] = {}
        dsv = d[session][video_ts]  # short name

        dsv['sent_ts'] = try_parsing_time(pt['time'])  # removed eventually
        dsv['size'] = float(pt['size'])  # bytes
        dsv['delivery_rate'] = float(pt['delivery_rate'])  # byte/second
        dsv['cwnd'] = float(pt['cwnd'])  # packets
        dsv['in_flight'] = float(pt['in_flight'])  # packets
        dsv['min_rtt'] = float(pt['min_rtt']) / MILLION  # us -> s
        dsv['rtt'] = float(pt['rtt']) / MILLION  # us -> s
        dsv['ssim_index'] = get_ssim_index(pt)  # unitless

    for pt in video_acked_results['video_acked']:
        session = (pt['user'], int(pt['init_id']), pt['expt_id'])
        if session not in d:
            continue

        video_ts = int(pt['video_ts'])
        if video_ts not in d[session]:
            continue

        dsv = d[session][video_ts]  # short name

        # calculate transmission time
        sent_ts = dsv['sent_ts']
        acked_ts = try_parsing_time(pt['time'])
        dsv['trans_time'] = (acked_ts - sent_ts).total_seconds()
        del dsv['sent_ts']

    # a pass on d to remove video_ts without trans_time
    sessions_to_remove = []
    for session in d:
        video_ts_to_remove = []
        for video_ts in d[session]:
            dsv = d[session][video_ts]  # short name

            if 'trans_time' not in dsv or dsv['trans_time'] <= 0:
                video_ts_to_remove.append(video_ts)
                continue

        for video_ts in video_ts_to_remove:
            del d[session][video_ts]

        if not d[session]:
            sessions_to_remove.append(session)

    for session in sessions_to_remove:
        del d[session]

    sys.stderr.write('Valid session count in video_data: {}\n'.format(len(d)))
    return d


def buffer_data_by_session(client_buffer_results):
    d = {}  # indexed by session

    excluded_sessions = {}
    last_ts = {}
    last_buf = {}
    last_cum_rebuf = {}
    last_low_buf = {}

    for pt in client_buffer_results['client_buffer']:
        session = (pt['user'], int(pt['init_id']), pt['expt_id'])
        if session in excluded_sessions:
            continue

        if session not in d:
            d[session] = {}
            d[session]['min_play_time'] = None
            d[session]['max_play_time'] = None
            d[session]['min_cum_rebuf'] = None
            d[session]['max_cum_rebuf'] = None
            d[session]['is_rebuffer'] = True
            d[session]['num_rebuf'] = 0
        ds = d[session]  # short name

        if session not in last_ts:
            last_ts[session] = None
        if session not in last_buf:
            last_buf[session] = None
        if session not in last_cum_rebuf:
            last_cum_rebuf[session] = None
        if session not in last_low_buf:
            last_low_buf[session] = None

        ts = try_parsing_time(pt['time'])
        buf = float(pt['buffer'])
        cum_rebuf = float(pt['cum_rebuf'])

        # update d[session]
        if pt['event'] == 'startup':
            ds['min_play_time'] = ts
            ds['min_cum_rebuf'] = cum_rebuf
            ds['is_rebuffer'] = False

        if ds['min_play_time'] is None or ds['min_cum_rebuf'] is None:
            # wait until 'startup' is found
            continue

        if pt['event'] == 'rebuffer':
            if not ds['is_rebuffer']:
                ds['num_rebuf'] += 1
            ds['is_rebuffer'] = True

        if pt['event'] == 'play':
            ds['is_rebuffer'] = False

        if not ds['is_rebuffer']:
            if ds['max_play_time'] is None or ts > ds['max_play_time']:
                ds['max_play_time'] = ts

            if ds['max_cum_rebuf'] is None or cum_rebuf > ds['max_cum_rebuf']:
                ds['max_cum_rebuf'] = cum_rebuf

        # verify that time is basically successive in the same session
        if last_ts[session] is not None:
            diff = (ts - last_ts[session]).total_seconds()
            if diff > 60:  # ambiguous / suspicious session
                sys.stderr.write('Ambiguous session: {}\n'.format(session))
                excluded_sessions[session] = True
                continue

        # identify outliers: exclude the sessions if there is a long rebuffer?
        if last_low_buf[session] is not None:
            diff = (ts - last_low_buf[session]).total_seconds()
            if diff > 30:
                sys.stderr.write('Outlier session: {}\n'.format(session))
                excluded_sessions[session] = True
                continue

        # identify stalls caused by slow video decoding
        if last_buf[session] is not None and last_cum_rebuf[session] is not None:
            if (buf > 5 and last_buf[session] > 5 and
                cum_rebuf > last_cum_rebuf[session] + 0.25):
                sys.stderr.write('Decoding stalls: {}\n'.format(session))
                excluded_sessions[session] = True
                continue

        # update last_XXX
        last_ts[session] = ts
        last_buf[session] = buf
        last_cum_rebuf[session] = cum_rebuf
        if buf > 0.1:
            last_low_buf[session] = None
        else:
            if last_low_buf[session] is None:
                last_low_buf[session] = ts

    ret = {}  # indexed by session

    # second pass to exclude short sessions
    short_session_cnt = 0  # count of short sessions

    for session in d:
        if session in excluded_sessions:
            continue

        ds = d[session]
        if ds['min_play_time'] is None or ds['min_cum_rebuf'] is None:
            # no 'startup' is found in the range
            continue

        sess_play = (ds['max_play_time'] - ds['min_play_time']).total_seconds()
        # exclude short sessions
        if sess_play < 5:
            short_session_cnt += 1
            continue

        sess_rebuf = ds['max_cum_rebuf'] - ds['min_cum_rebuf']
        if sess_rebuf > 300:
            sys.stderr.write('Warning: bad session (rebuffer > 5min): {}\n'
                             .format(session))

        if session not in ret:
            ret[session] = {}

        ret[session]['play'] = sess_play
        ret[session]['rebuf'] = sess_rebuf
        ret[session]['startup'] = ds['min_cum_rebuf']
        ret[session]['num_rebuf'] = ds['num_rebuf']

    sys.stderr.write('Short session (play < 5s) count in buffer_data: {}\n'
                     .format(short_session_cnt))
    sys.stderr.write('Valid session count in buffer_data: {}\n'
                     .format(len(ret)))
    return ret
