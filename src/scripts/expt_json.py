#!/usr/bin/env python3

import argparse
import yaml
import json
from os import path
from subprocess import check_output


def get_git_commit_hash():
    curr_dir = path.dirname(path.abspath(__file__))
    return check_output(['git', 'rev-parse', 'HEAD'],
                        cwd=curr_dir).decode().strip()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('yaml_settings')
    args = parser.parse_args()

    git_commit_hash = get_git_commit_hash()

    # add git commit hash to YAML
    with open(args.yaml_settings, 'r') as fh:
        yaml_settings = yaml.safe_load(fh)
    yaml_settings['git_commit_hash'] = git_commit_hash

    # convert YAML to JSON-compatible string
    print(json.dumps(yaml_settings))


if __name__ == '__main__':
    main()
