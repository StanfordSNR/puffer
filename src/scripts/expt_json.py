#!/usr/bin/env python3

import argparse
import yaml
import json
from os import path
from subprocess import check_output


def git_commit_hash():
    curr_dir = path.dirname(path.abspath(__file__))
    return check_output(['git', 'rev-parse', 'HEAD'],
                        cwd=curr_dir).decode().strip()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('yaml_string')
    args = parser.parse_args()

    parsed_yaml = yaml.safe_load(args.yaml_string)

    # add git commit hash to YAML
    parsed_yaml['git_commit'] = git_commit_hash()

    # convert YAML-compatible string to JSON-compatible string
    print(json.dumps(parsed_yaml))


if __name__ == '__main__':
    main()
