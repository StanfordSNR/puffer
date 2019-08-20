import sys
import numpy as np

from helpers import get_abr_cc, retrieve_expt_config


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
    def __init__(self, expt={}, postgres_cursor=None):
        self.expt = expt
        self.postgres_cursor = postgres_cursor

        # key: (abr, cc); value: {'total_play': ...; 'total_rebuf': ...}
        self.out = {}

        self.smap = {}  # key: session ID; value: {}
        self.expiry_list = ExpiryList(np.timedelta64(1, 'm'))

    def empty_session(self):
        s = {}
        s['valid'] = True
        s['node'] = None

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
        if session not in self.smap:
            self.smap[session] = self.empty_session()
            node = ListNode(ts, session)

            self.smap[session]['node'] = node
            self.expiry_list.append(node)
        else:
            node = self.smap[session]['node']

            self.expiry_list.remove(node)
            node.ts = ts
            self.expiry_list.append(node)

    def valid_expired_session(self, session):
        s = self.smap[session]
        if not s['valid']:
            return False

        for k in ['min_play_time', 'max_play_time',
                  'min_cum_rebuf', 'max_cum_rebuf']:
            if s[k] is None:  # no 'startup' is found in the session
                sys.stderr.write('No startup found in session: {}\n'
                                 .format(session))
                s['valid'] = False
                return False

        session_play = ((s['max_play_time'] - s['min_play_time'])
                        / np.timedelta64(1, 's'))
        if session_play < 5:  # session is too short
            sys.stderr.write('Session is too short: {}, {}s\n'
                             .format(session, session_play))
            s['valid'] = False
            return False

        session_rebuf = s['max_cum_rebuf'] - s['min_cum_rebuf']
        if session_rebuf > 300:
            sys.stderr.write('Warning: session with long rebuffering '
                             '(rebuffer > 5min): {}\n'.format(session))

        return True

    def process_expired(self):
        # read and process expired values
        expired_sessions = self.expiry_list.expired

        for session in expired_sessions:
            s = self.smap[session]

            if not self.valid_expired_session(session):
                del self.smap[session]
                continue

            expt_id = str(session[-1])
            expt_config = retrieve_expt_config(expt_id, self.expt,
                                               self.postgres_cursor)

            abr_cc = get_abr_cc(expt_config)
            if abr_cc not in self.out:
                self.out[abr_cc] = {}
                self.out[abr_cc]['total_play'] = 0
                self.out[abr_cc]['total_rebuf'] = 0

            session_play = ((s['max_play_time'] - s['min_play_time'])
                            / np.timedelta64(1, 's'))
            session_rebuf = s['max_cum_rebuf'] - s['min_cum_rebuf']

            self.out[abr_cc]['total_play'] += session_play
            self.out[abr_cc]['total_rebuf'] += session_rebuf

            del self.smap[session]

        # clean processed expired values
        self.expiry_list.expired = []

    def valid_active_session(self, pt, ts, session, buf, cum_rebuf):
        s = self.smap[session]  # short variable name
        if not s['valid']:
            return False

        # verify that time is basically successive in the same session
        if s['last_ts'] is not None:
            diff = (ts - s['last_ts']) / np.timedelta64(1, 's')
            if diff > 60:  # ambiguous / suspicious session
                sys.stderr.write('Ambiguous session: {}\n'.format(session))
                s['valid'] = False
                return False

        # identify outliers: exclude the sessions if there is a long rebuffer?
        if s['last_low_buf'] is not None:
            diff = (ts - s['last_low_buf']) / np.timedelta64(1, 's')
            if diff > 30:
                sys.stderr.write('Outlier session: {}\n'.format(session))
                s['valid'] = False
                return False

        # identify stalls caused by slow video decoding
        if s['last_buf'] is not None and s['last_cum_rebuf'] is not None:
            if (buf > 5 and s['last_buf'] > 5 and
                cum_rebuf > s['last_cum_rebuf'] + 0.25):
                sys.stderr.write('Decoding stalls: {}\n'.format(session))
                s['valid'] = False
                return False

        return True

    def process_pt(self, pt, ts, session):
        s = self.smap[session]  # short variable name

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

        # update smap and expiry_list
        self.update_map_list(ts, session)

        # process expired sessions
        self.process_expired()

        # process the current data point
        self.process_pt(pt, ts, session)

    # called after the last data point to process the remaining data
    def done(self):
        self.expiry_list.expire_all()
        self.process_expired()
