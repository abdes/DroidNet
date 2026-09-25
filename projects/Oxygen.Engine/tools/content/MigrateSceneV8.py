"""Migrate scene descriptor v7 to v8; canonical import accepts only v8."""

import argparse
import copy
import json
from pathlib import Path


def migrate(document):
    if not isinstance(document, dict) or type(document.get("version")) is not int or document["version"] != 7:
        raise ValueError("Expected a scene descriptor with integer version 7")
    result = copy.deepcopy(document)
    sky = result.get("environment", {}).get("sky_light", {})
    sky.pop("real_time_capture_enabled", None)
    result["version"] = 8
    if result.get("$schema") == "oxygen.scene-descriptor.v7":
        result["$schema"] = "oxygen.scene-descriptor.v8"
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path, help="Destination descriptor; may be the source path")
    args = parser.parse_args()
    raw = args.source.read_bytes()
    result = migrate(json.loads(raw.decode("utf-8-sig")))
    encoding = "utf-8-sig" if raw.startswith(b"\xef\xbb\xbf") else "utf-8"
    args.output.write_text(json.dumps(result, indent=2) + "\n", encoding=encoding, newline="\n")


if __name__ == "__main__":
    main()
