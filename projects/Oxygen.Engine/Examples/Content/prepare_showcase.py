"""Stage the shipped authoring inputs; native tools remain the cooking authority."""

import argparse
import json
import os
from pathlib import Path


def write_changed(path, data):
    path.parent.mkdir(parents=True, exist_ok=True)
    if not path.exists() or path.read_bytes() != data:
        path.write_bytes(data)


def prepare(source, output, scenes):
    visited = set()
    excluded_jobs = {
        "scene.physics_domains_vsm_benchmark",
        "script.physics_domains_benchmark_camera",
        "script.sidecar.physics_domains_vsm_benchmark",
        "physics.sidecar.park_vsm_benchmark",
    }

    def copy_input(path):
        path = path.resolve()
        if not path.is_relative_to(source):
            raise ValueError(f"Showcase input is outside Content: {path}")
        if path in visited:
            return
        visited.add(path)
        target = output / path.relative_to(source)
        data = path.read_bytes()
        if path.suffix == ".json":
            obj = json.loads(data)
            if path.name == "import-manifest.json":
                obj["jobs"] = [job for job in obj["jobs"] if job.get("id") not in excluded_jobs]

            def visit(value):
                if isinstance(value, dict):
                    for key, child in value.items():
                        if key == "$schema":
                            value[key] = Path(os.path.relpath(output.parent / "schemas" / Path(child).name, target.parent)).as_posix()
                            continue
                        visit(child)
                elif isinstance(value, list):
                    for child in value:
                        visit(child)
                elif isinstance(value, str) and not value.startswith(("/", "asset:", "http:" , "https:")):
                    candidate = path.parent / value
                    if candidate.is_file():
                        copy_input(candidate)

            visit(obj)
            data = (json.dumps(obj, indent=2) + "\n").encode()
        write_changed(target, data)

    for scene in scenes:
        copy_input(source / "scenes" / scene / "import-manifest.json")
    for name in ("images/showcase/Sky.hdr", "README.md", "ASSET_CREDITS.md",
                 "showcase-assets.json", "cook_scenes.ps1", "cook_scenes.cmd",
                 "pak_content.ps1", "pak_content.cmd"):
        copy_input(source / name)
    write_changed(output.parent / "inputs.txt", ("\n".join(p.as_posix() for p in sorted(visited)) + "\n").encode())


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--scenes", required=True, nargs="+")
    args = parser.parse_args()
    prepare(args.source.resolve(), args.output.resolve(), args.scenes)
