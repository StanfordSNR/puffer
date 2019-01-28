#!/usr/bin/env python3

import argparse
import yaml
import pickle

from helpers import connect_to_postgres, retrieve_expt_config


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('yaml_settings')
    args = parser.parse_args()

    with open(args.yaml_settings, 'r') as fh:
        yaml_settings = yaml.safe_load(fh)

    # create a Postgres client and perform queries
    postgres_client = connect_to_postgres(yaml_settings)
    postgres_cursor = postgres_client.cursor()

    expt_id_cache = {}
    expt_id = 0

    try:
        while True:
            expt_id += 1
            retrieve_expt_config(expt_id, expt_id_cache, postgres_cursor)
    except:
        print('Max valid expt_id = {}'.format(expt_id - 1))
    finally:
        postgres_cursor.close()

        file_name = 'expt_id_cache.pickle'
        with open(file_name, 'wb') as fh:
            pickle.dump(expt_id_cache, fh, protocol=pickle.HIGHEST_PROTOCOL)
        print('Saved expt_id_cache to {}'.format(file_name))

if __name__ == '__main__':
    main()
