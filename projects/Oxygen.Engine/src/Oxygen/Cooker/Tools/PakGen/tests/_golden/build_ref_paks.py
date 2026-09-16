import pathlib
import hashlib
import json

from pakgen.api import BuildOptions, build_pak, inspect_pak


def build(spec_path: pathlib.Path, out_pak: pathlib.Path) -> None:
    build_pak(
        BuildOptions(
            input_spec=spec_path,
            output_path=out_pak,
            deterministic=True,
        )
    )
    lock_path = out_pak.with_suffix(".lock.json")
    if lock_path.exists():
        payload = out_pak.read_bytes()
        info = inspect_pak(out_pak)
        lock = {
            "file_size": len(payload),
            "sha256": hashlib.sha256(payload).hexdigest(),
            "pak_crc32": info["footer"]["real_crc32"],
            "physics_table_count": info["footer"]["tables"]["physics"]["count"],
            "asset_count": info["footer"]["directory"]["asset_count"],
        }
        lock_path.write_text(
            json.dumps(lock, indent=2) + "\n", encoding="utf-8", newline="\n"
        )
    print("WROTE", out_pak, out_pak.stat().st_size)


root = pathlib.Path(__file__).parent

build(root / "minimal_spec.yaml", root / "minimal_ref.pak")
build(root / "scene_basic_spec.yaml", root / "scene_basic.pak")
build(root / "scripting_scene_spec.yaml", root / "scripting_scene_ref.pak")
build(root / "input_scene_spec.yaml", root / "input_scene_ref.pak")
build(
    root / "scene_with_physics_sidecar_spec.yaml",
    root / "scene_with_physics_sidecar_ref.pak",
)
