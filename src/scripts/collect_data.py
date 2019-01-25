from helpers import try_parsing_time, get_ssim_index
from plot_ssim_rebuffer import collect_buffer_data


VIDEO_DURATION = 180180
PKT_BYTES = 1500
MILLION = 1000000


def video_data_by_session(video_sent_results, video_acked_results):
    d = {}
    last_video_ts = {}

    for pt in video_sent_results['video_sent']:
        session = (pt['user'], int(pt['init_id']), int(pt['expt_id']))
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
        session = (pt['user'], int(pt['init_id']), int(pt['expt_id']))
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

    return d


def buffer_data_by_session(client_buffer_results):
    return collect_buffer_data(client_buffer_results)
