import pathlib

from pakgen.api import BuildOptions, build_pak


def build(spec_path: pathlib.Path, out_pak: pathlib.Path) -> None:
    build_pak(BuildOptions(input_spec=spec_path, output_path=out_pak))
    print("WROTE", out_pak, out_pak.stat().st_size)


root = pathlib.Path(__file__).parent

build(root / "minimal_spec.yaml", root / "minimal_ref.pak")
build(root / "scene_basic_spec.yaml", root / "scene_basic.pak")
