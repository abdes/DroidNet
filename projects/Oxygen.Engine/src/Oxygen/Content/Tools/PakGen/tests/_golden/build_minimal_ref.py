import json, pathlib
from pakgen.api import BuildOptions, build_pak

root = pathlib.Path(__file__).parent
spec_path = root / "minimal_spec.json"
out_pak = root / "minimal_ref.pak"
build_pak(BuildOptions(input_spec=spec_path, output_path=out_pak))
print("WROTE", out_pak, out_pak.stat().st_size)
