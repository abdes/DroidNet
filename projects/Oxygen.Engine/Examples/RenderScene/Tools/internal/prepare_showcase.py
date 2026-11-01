"""Stage RenderScene's installed showcase.

The stage holds what the SDK ships under share/oxygen:

- `Showcase/`: the examples project reduced to one tag. Project.oxy,
  Examples.json filtered to the tag, those examples' manifests, the Showcase
  mount files their jobs and model import records reach, and the project's own
  tools (fetch and the schemas).
- `RenderScene/Tools/`: RenderScene's cook, pak and play tools, which cook the
  staged project into RenderScene's own `.cooked/` and `.build/`.

Native tools remain the cooking authority; this script only copies.
"""

import argparse
import json
import shutil
from pathlib import Path

# RenderScene's tools, which this script sits among.
RENDERSCENE_TOOLS = Path(__file__).resolve().parents[1]
# The ones an SDK user runs, relative to RenderScene/Tools; this script stays in
# the source tree.
TOOLS = [
    "cook.ps1",
    "cook.cmd",
    "pak.ps1",
    "pak.cmd",
    "play.ps1",
    "play.cmd",
    "internal/ExamplesTools.ps1",
]
# Files of the project every staged copy has, beyond what the manifests reach;
# the generators stay in the source tree.
PROJECT_FILES = [
    "Project.oxy",
    "README.md",
    "Content/SourceMedia/ASSET_CREDITS.md",
    "Content/SourceMedia/showcase-assets.json",
    "Tools/fetch.ps1",
    "Tools/fetch.cmd",
    "Tools/examples.schema.json",
    "Tools/showcase-assets.schema.json",
]


# The tools' outputs in a staged project, which the build removes itself.
OUTPUTS = (".cooked", ".build")
# The mount the examples' identities and mount paths are in.
MOUNT = "Showcase"
MOUNT_PREFIX = f"/{MOUNT}/"


def mount_folder(source: Path) -> Path:
    """The folder of the project's `Showcase` mount, as Project.oxy names it."""
    project = json.loads((source / "Project.oxy").read_text(encoding="utf-8"))
    folders = [m["Path"] for m in project["Mounts"] if m["Name"] == MOUNT]
    if len(folders) != 1:
        raise ValueError(f"Project.oxy has no {MOUNT} mount")
    return (source / folders[0]).resolve()


def mount_file(content: Path, mount_path: str) -> Path:
    """The file of `mount_path`, a mount path in the Showcase mount."""
    if not mount_path.startswith(MOUNT_PREFIX):
        raise ValueError(f"{mount_path} is not in the {MOUNT} mount")
    return content / mount_path[len(MOUNT_PREFIX) :]


def write_changed(path: Path, data: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    if not path.exists() or path.read_bytes() != data:
        path.write_bytes(data)


def prune(output: Path, staged: set[Path]) -> None:
    """Remove what an earlier staging left that this one does not stage.

    A folder spelled in another case than the staged path, such as an older
    `manifests/` for `Manifests/`, is removed: on a case-insensitive file system it
    would keep its spelling, and a record must sit at its exact path.
    """
    if not output.is_dir():
        return
    keep = {path.relative_to(output).as_posix() for path in staged}
    folders = {parent.as_posix() for name in keep for parent in Path(name).parents}
    for entry in sorted(output.rglob("*"), reverse=True):
        relative = entry.relative_to(output).as_posix()
        if relative.split("/")[0] in OUTPUTS:
            continue
        if entry.is_dir():
            if relative not in folders:
                shutil.rmtree(entry)
        elif relative not in keep:
            entry.unlink()


def strings(value: object):
    if isinstance(value, dict):
        for child in value.values():
            yield from strings(child)
    elif isinstance(value, list):
        for child in value:
            yield from strings(child)
    elif isinstance(value, str):
        yield value


def prepare(source: Path, stage: Path, tag: str) -> list[Path]:
    """Stage the examples tagged `tag` and RenderScene's tools in `stage`.

    Returns every input read.
    """
    output = stage / "Showcase"
    content = mount_folder(source)
    inputs: set[Path] = set()
    copied: set[Path] = set()
    staged: set[Path] = {output / "Examples.json"}

    def copy(path: Path) -> None:
        path = path.resolve()
        if path in copied:
            return
        if not path.is_relative_to(source):
            raise ValueError(f"Showcase input is outside the project: {path}")
        copied.add(path)
        inputs.add(path)
        staged.add(output / path.relative_to(source))
        if path.suffix != ".json":
            return
        # A file a JSON input names, by mount path or relative to the file,
        # ships with it: descriptor images, buffers, scripts, records.
        for text in strings(json.loads(path.read_text(encoding="utf-8"))):
            if text.startswith(MOUNT_PREFIX):
                candidate = mount_file(content, text)
            elif text.startswith("/") or ":" in text:
                continue
            else:
                candidate = path.parent / text
            if candidate.is_file() and candidate.resolve().is_relative_to(content):
                copy(candidate)

    catalog_path = source / "Examples.json"
    inputs.add(catalog_path.resolve())
    catalog = json.loads(catalog_path.read_text(encoding="utf-8"))
    selected = [e for e in catalog["examples"] if tag in e["tags"]]
    if not selected:
        raise ValueError(f"No example has the tag '{tag}'")
    write_changed(
        output / "Examples.json",
        (json.dumps({**catalog, "examples": selected}, indent=2) + "\n").encode(),
    )
    for example in selected:
        manifest = source / "Manifests" / f"{example['name']}.import-manifest.json"
        copy(manifest)
        for job in json.loads(manifest.read_text(encoding="utf-8"))["jobs"]:
            if "source" in job:
                copy(mount_file(content, job["source"]))
            if job["type"] == "model":
                # A model's record lives at its identity in the Showcase mount,
                # and names its source relative to itself.
                copy(mount_file(content, f"{job['virtual_path']}.import.json"))
    for name in PROJECT_FILES:
        copy(source / name)
    prune(output, staged)
    for path in sorted(copied):
        write_changed(output / path.relative_to(source), path.read_bytes())

    tools = stage / "RenderScene" / "Tools"
    tool_files = {tools / name: RENDERSCENE_TOOLS / name for name in TOOLS}
    prune(tools, set(tool_files))
    for target, tool in sorted(tool_files.items()):
        inputs.add(tool)
        write_changed(target, tool.read_bytes())
    return sorted(inputs)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", required=True, type=Path)
    parser.add_argument(
        "--stage",
        required=True,
        type=Path,
        help="Receives Showcase/ and RenderScene/Tools/, as share/oxygen holds them",
    )
    parser.add_argument("--tag", required=True)
    parser.add_argument(
        "--inputs",
        required=True,
        type=Path,
        help="Receives every input read, one per line, for the build to watch",
    )
    args = parser.parse_args()
    inputs = prepare(args.source.resolve(), args.stage.resolve(), args.tag)
    write_changed(
        args.inputs, ("\n".join(p.as_posix() for p in inputs) + "\n").encode()
    )


if __name__ == "__main__":
    main()
