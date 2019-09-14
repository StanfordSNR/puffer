import sys
import numpy as np

from helpers import (get_abr_cc, retrieve_expt_config, get_ssim_index,
                     datetime_iter, query_measurement)


VIDEO_DURATION = 180180
MILLION = 1000000


class ListNode:
    def __init__(self, ts=None, val=None):
        self.prev = None
        self.next = None
        self.ts = ts
        self.val = val


class ExpiryList:
    def __init__(self, expiry):
        self.head = ListNode()
        self.tail = ListNode()
        self.expiry = expiry

        self.head.next = self.tail
        self.tail.prev = self.head

        self.expired = []

    # remove node from list
    def remove(self, node):
        node_prev = node.prev
        node_next = node.next
        node_prev.next = node_next
        node_next.prev = node_prev

    # append node to the rear of list
    def append(self, node):
        tail_prev = self.tail.prev
        if tail_prev.ts is not None and tail_prev.ts > node.ts:
            sys.exit('Error: cannot append a node with a smaller '
                     'timestamp {}'.format(node.ts))

        tail_prev.next = node
        self.tail.prev = node
        node.prev = tail_prev
        node.next = self.tail

        # keep removing the smallest ts if it is expired
        n = self.head.next
        while n != self.tail:
            if node.ts - n.ts <= self.expiry:
                break

            self.remove(n)
            self.expired.append(n.val)
            n = n.next

    # move all list nodes to expired
    def expire_all(self):
        n = self.head.next
        while n != self.tail:
            self.remove(n)
            self.expired.append(n.val)
            n = n.next

    # list traversal
    def traverse(self):
        n = self.head.next
        while n != self.tail:
            print(n.ts, n.val)
            n = n.next


class BufferStream:
    def __init__(self, callback):
        self.callback = callback

        self.session_node = {}  # { session ID: ListNode }
        self.expiry_list = ExpiryList(np.timedelta64(1, 'm'))

        # private session information
        self.session_info = {}  # { session ID: { 'FIELD': value } }

    def empty_session(self):
        s = {}
        s['valid'] = True  # whether this session is valid and ever used

        for k in ['min_play_time', 'max_play_time',
                  'min_cum_rebuf', 'max_cum_rebuf']:
            s[k] = None

        s['is_rebuffer'] = True
        s['num_rebuf'] = 0

        s['last_ts'] = None
        s['last_buf'] = None
        s['last_cum_rebuf'] = None
        s['last_low_buf'] = None

        return s

    def update_map_list(self, ts, session):
        if session not in self.session_node:
            node = ListNode(ts, session)
            self.session_node[session] = node
            self.expiry_list.append(node)

            self.session_info[session] = self.empty_session()
        else:
            node = self.session_node[session]

            self.expiry_list.remove(node)
            node.ts = ts
            self.expiry_list.append(node)

    def valid_active_session(self, pt, ts, session, buf, cum_rebuf):
        s = self.session_info[session]  # short name
        if not s['valid']:
            return False

        # verify that time is basically successive in the same session
        if s['last_ts'] is not None:
            diff = (ts - s['last_ts']) / np.timedelta64(1, 's')
            if diff > 60:  # nonconsecutive session
                sys.stderr.write('Nonconsecutive: {}\n'.format(session))
                s['valid'] = False
                return False

        # detect sessions with long rebuffer durations
        if s['last_low_buf'] is not None:
            diff = (ts - s['last_low_buf']) / np.timedelta64(1, 's')
            if diff > 30:
                sys.stderr.write('Long rebuffer: {}\n'.format(session))
                s['valid'] = False
                return False

        # detect sessions with stalls caused by slow video decoding
        if s['last_buf'] is not None and s['last_cum_rebuf'] is not None:
            if (buf > 5 and s['last_buf'] > 5 and
                cum_rebuf > s['last_cum_rebuf'] + 0.25):
                sys.stderr.write('Decoding stalls: {}\n'.format(session))
                s['valid'] = False
                return False

        return True

    def valid_expired_session(self, session):
        s = self.session_info[session]  # short name
        if not s['valid']:
            return False

        # basic validity checks below
        for k in ['min_play_time', 'max_play_time',
                  'min_cum_rebuf', 'max_cum_rebuf']:
            if s[k] is None:  # no 'startup' is found in the session
                sys.stderr.write('No startup: {}\n'
                                 .format(session))
                s['valid'] = False
                return False

        return True

    def process_pt(self, pt, ts, session):
        s = self.session_info[session]  # short name

        # process the current data points
        buf = float(pt['buffer'])
        cum_rebuf = float(pt['cum_rebuf'])

        # verify if the currently active session is still valid
        if not self.valid_active_session(pt, ts, session, buf, cum_rebuf):
            return

        if s['min_play_time'] is None and pt['event'] != 'startup':
            # wait until 'startup' is found
            return

        if pt['event'] == 'startup':
            s['min_play_time'] = ts
            s['min_cum_rebuf'] = cum_rebuf
            s['is_rebuffer'] = False
        elif pt['event'] == 'rebuffer':
            if not s['is_rebuffer']:
                s['num_rebuf'] += 1
            s['is_rebuffer'] = True
        elif pt['event'] == 'play':
            s['is_rebuffer'] = False

        if not s['is_rebuffer']:
            if s['max_play_time'] is None or ts > s['max_play_time']:
                s['max_play_time'] = ts

            if s['max_cum_rebuf'] is None or cum_rebuf > s['max_cum_rebuf']:
                s['max_cum_rebuf'] = cum_rebuf

        # update last_XXX
        s['last_ts'] = ts
        s['last_buf'] = buf
        s['last_cum_rebuf'] = cum_rebuf
        if buf > 0.1:
            s['last_low_buf'] = None
        else:
            if s['last_low_buf'] is None:
                s['last_low_buf'] = ts

    def add_data_point(self, pt):
        session = (pt['user'], pt['init_id'], pt['expt_id'])
        ts = np.datetime64(pt['time'])

        # update session_node and expiry_list
        self.update_map_list(ts, session)

        # process the current data point
        self.process_pt(pt, ts, session)

    def process_expired_sessions(self):
        # call the callback function with each session
        for session in self.expiry_list.expired:
            if not self.valid_expired_session(session):
                continue

            s = self.session_info[session]  # short name

            # construct out
            out = {}
            out['play_time'] = ((s['max_play_time'] - s['min_play_time'])
                                / np.timedelta64(1, 's'))
            out['cum_rebuf'] = s['max_cum_rebuf'] - s['min_cum_rebuf']
            out['num_rebuf'] = s['num_rebuf']
            out['startup_delay'] = s['min_cum_rebuf']

            self.callback(session, out)

        # clean up sessions
        for session in self.expiry_list.expired:
            del self.session_node[session]
            del self.session_info[session]
        self.expiry_list.expired = []

    def do_process(self, influx_client, s_str, e_str):
        client_buffer_results = query_measurement(
            influx_client, 'client_buffer', s_str, e_str)['client_buffer']

        for pt in client_buffer_results:
            self.add_data_point(pt)
            self.process_expired_sessions()

    def process(self, influx_client, start_time, end_time):
        for s_str, e_str in datetime_iter(start_time, end_time):
            sys.stderr.write('Processing client_buffer data '
                             'between {} and {}\n'.format(s_str, e_str))
            self.do_process(influx_client, s_str, e_str)

        self.expiry_list.expire_all()
        self.process_expired_sessions()


class VideoStream:
    def __init__(self, callback):
        self.callback = callback

        self.out = {}  # { session ID: { video ts: { 'FIELD': value } } }
        self.session_node = {}  # { session ID: ListNode }
        self.expiry_list = ExpiryList(np.timedelta64(1, 'm'))

    def update_map_list(self, ts, session):
        if session not in self.session_node:
            node = ListNode(ts, session)
            self.session_node[session] = node
            self.expiry_list.append(node)

            self.out[session] = {}
        else:
            node = self.session_node[session]

            self.expiry_list.remove(node)
            node.ts = ts
            self.expiry_list.append(node)

    def process_video_sent_pt(self, pt, ts, session):
        video_ts = int(pt['video_ts'])

        if video_ts in self.out[session]:
            sys.exit('VideoStream: video_ts {} already exists in session {}'
                     .format(video_ts, session))

        self.out[session][video_ts] = {}
        sv = self.out[session][video_ts]  # short name

        sv['sent_ts'] = ts  # np.datetime64
        sv['format'] = pt['format']  # e.g., '1280x720-24'
        sv['size'] = float(pt['size'])  # bytes
        sv['delivery_rate'] = float(pt['delivery_rate'])  # byte/second
        sv['cwnd'] = float(pt['cwnd'])  # packets
        sv['in_flight'] = float(pt['in_flight'])  # packets
        sv['min_rtt'] = float(pt['min_rtt']) / MILLION  # us -> s
        sv['rtt'] = float(pt['rtt']) / MILLION  # us -> s
        sv['ssim_index'] = get_ssim_index(pt)  # unitless float
        sv['channel'] = pt['channel']  # e.g., 'cbs'

    def process_video_acked_pt(self, pt, ts, session):
        if session not in self.out:
            return

        video_ts = int(pt['video_ts'])
        if video_ts not in self.out[session]:
            return

        sv = self.out[session][video_ts]  # short name
        sv['trans_time'] = (ts - sv['sent_ts']) / np.timedelta64(1, 's')  # s

    def add_data_point(self, pt, measurement):
        if measurement != 'video_sent' and measurement != 'video_acked':
            sys.exit('VideoStream: measurement {} is not supported'
                     .format(measurement))

        session = (pt['user'], pt['init_id'], pt['expt_id'])
        ts = np.datetime64(pt['time'])

        # update session_node and expiry_list
        self.update_map_list(ts, session)

        # process the current data point
        if measurement == 'video_sent':
            self.process_video_sent_pt(pt, ts, session)
        elif measurement == 'video_acked':
            self.process_video_acked_pt(pt, ts, session)

    def process_expired_sessions(self):
        # call the callback function with each session
        for session in self.expiry_list.expired:
            # remove video_ts that doesn't have trans_time
            video_ts_to_remove = []

            for video_ts in self.out[session]:
                sv = self.out[session][video_ts]  # short name

                if 'trans_time' not in sv or sv['trans_time'] <= 0:
                    video_ts_to_remove.append(video_ts)

            for video_ts in video_ts_to_remove:
                del self.out[session][video_ts]

            if self.out[session]:
                self.callback(session, self.out[session])

        # clean up sessions
        for session in self.expiry_list.expired:
            del self.session_node[session]
            del self.out[session]
        self.expiry_list.expired = []

    def do_process(self, influx_client, s_str, e_str):
        # process the data point with smaller ts from two lists of results
        gen = {}
        gen['video_sent'] = query_measurement(
            influx_client, 'video_sent', s_str, e_str)['video_sent']
        gen['video_acked'] = query_measurement(
            influx_client, 'video_acked', s_str, e_str)['video_acked']

        # maintain iterators for the two lists
        it = {}
        for measurement in ['video_sent', 'video_acked']:
            it[measurement] = next(gen[measurement], None)

        while True:
            next_ts = None
            next_measurement = None

            for measurement in ['video_sent', 'video_acked']:
                item = it[measurement]
                if not item:
                    continue

                ts = np.datetime64(item['time'])
                if next_ts is None or ts < next_ts:
                    next_ts = ts
                    next_measurement = measurement

            if next_measurement is None:  # no data left in either list
                break

            self.add_data_point(it[next_measurement], next_measurement)
            it[next_measurement] = next(gen[next_measurement], None)

            self.process_expired_sessions()

    def process(self, influx_client, start_time, end_time):
        for s_str, e_str in datetime_iter(start_time, end_time):
            sys.stderr.write('Processing video_sent and video_acked data '
                             'between {} and {}\n'.format(s_str, e_str))
            self.do_process(influx_client, s_str, e_str)

        self.expiry_list.expire_all()
        self.process_expired_sessions()
