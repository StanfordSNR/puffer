from ttp import prepare_raw_data


VIDEO_DURATION = 180180
PKT_BYTES = 1500
MILLION = 1000000


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
