#!/usr/bin/env python3

import argparse
import sys
import yaml
import json

from helpers import connect_to_postgres


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('yaml_settings')
    parser.add_argument('-o', metavar='OUTPUT',
                        help='output path (default: expt_cache.json)')
    args = parser.parse_args()

    with open(args.yaml_settings, 'r') as fh:
        yaml_settings = yaml.safe_load(fh)

    output_path = args.o if args.o else 'expt_cache.json'

    # create a Postgres client and perform queries
    postgres_client = connect_to_postgres(yaml_settings)
    postgres_cursor = postgres_client.cursor()

    postgres_cursor.execute('SELECT * FROM puffer_experiment;')
    rows = postgres_cursor.fetchall()
    postgres_cursor.close()

    # read and dump expt_cache into a JSON file
    expt_cache = {}
    for row in rows:
        expt_id = row[0]
        # expt_config_hash = row[1]
        expt_config = row[2]

        if expt_id not in expt_cache:
            expt_cache[expt_id] = expt_config
        else:
            sys.exit('expt_id {} already exists'.format(expt_id))

    with open(output_path, 'w') as fh:
        json.dump(expt_cache, fh)
    sys.stderr.write('Saved to {}\n'.format(output_path))


if __name__ == '__main__':
    main()
