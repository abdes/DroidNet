"""Prepare clangd databases from CMake's commands, without changing any flags.

Run by CMake Tools' post-configure task. Each build tree/configuration owns its
output, so configuring another tree never selects an editor configuration.
"""

import argparse
import json
import os
from pathlib import Path
import re
import tempfile


def read_cache(path):
    values = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        if not line or line.startswith(("//", "#")):
            continue
        key, separator, value = line.partition("=")
        if separator:
            values[key.partition(":")[0]] = value
    return values


def split_commands(commands, configurations, multi_config):
    """Use the configuration segment of CMake's Ninja object output paths.

    Unknown output layouts are errors, never guessed or assigned to every
    configuration. Keep duplicate source entries belonging to distinct targets.
    """
    result = {config: [] for config in configurations}
    # CMake 4.2 supports both named FULL directories and hashed SHORT ones.
    output_pattern = re.compile(
        r"(?:^|/)(?:CMakeFiles/[^/]+\.dir|\.o/[0-9a-fA-F]+)/([^/]+)/"
    )
    for command in commands:
        if not isinstance(command, dict) or not all(
            isinstance(command.get(key), str) for key in ("file", "directory")
        ) or not (isinstance(command.get("command"), str)
                  or isinstance(command.get("arguments"), list)):
            raise ValueError("Invalid compilation database entry")
        if multi_config:
            output = command.get("output", "")
            match = output_pattern.search(output.replace("\\", "/"))
            if not match or match[1] not in result:
                raise ValueError(
                    f"Cannot determine configuration for {command['file']}: {output!r}"
                )
            config = match[1]
        else:
            config = configurations[0]
        result[config].append(command)
    return result


def write_if_changed(path, data):
    encoded = (json.dumps(data, indent=2, ensure_ascii=False) + "\n").encode("utf-8")
    if path.is_file() and path.read_bytes() == encoded:
        return
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(dir=path.parent, delete=False) as stream:
            temporary = Path(stream.name)
            stream.write(encoded)
        os.replace(temporary, path)
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)


def prepare(build_directory):
    build = Path(build_directory).resolve()
    cache = read_cache(build / "CMakeCache.txt")
    generator = cache.get("CMAKE_GENERATOR")
    if generator not in ("Ninja", "Ninja Multi-Config"):
        raise ValueError("VS Code/clangd requires a Ninja build tree; VS trees belong to Visual Studio")
    multi = generator == "Ninja Multi-Config"
    configs = (cache.get("CMAKE_CONFIGURATION_TYPES", "").split(";") if multi
               else [cache.get("CMAKE_BUILD_TYPE", "")])
    if not configs or any(not re.fullmatch(r"[A-Za-z0-9_][A-Za-z0-9_.+-]*", c) for c in configs):
        raise ValueError("Select explicit, valid CMake configurations before preparing clangd")
    commands = json.loads((build / "compile_commands.json").read_text(encoding="utf-8"))
    if not isinstance(commands, list) or not commands:
        raise ValueError("CMake produced no compilation commands; configure a compiled target first")
    databases = split_commands(commands, configs, multi)
    # Validate the entire input before publishing any configuration.
    for config, entries in databases.items():
        write_if_changed(build / "clangd" / config / "compile_commands.json", entries)
    return {config: len(entries) for config, entries in databases.items()}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", required=True)
    args = parser.parse_args()
    try:
        counts = prepare(args.build_dir)
    except (OSError, ValueError) as error:
        parser.exit(1, f"Clangd database preparation failed: {error}\n")
    print("Prepared clangd databases: " + ", ".join(f"{k}={v}" for k, v in counts.items()))


if __name__ == "__main__":
    main()
