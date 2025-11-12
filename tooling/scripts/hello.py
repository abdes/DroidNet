#!/usr/bin/env python3
import argparse
import sys


def run():
    parser = argparse.ArgumentParser(prog="hello")
    parser.add_argument("--name", default="world")
    args = parser.parse_args()
    print(f"Hello, {args.name}!")
    return 0


if __name__ == "__main__":
    sys.exit(run())
