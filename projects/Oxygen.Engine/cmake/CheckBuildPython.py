"""Read-only verification of the selected Python build-tool environment."""

import hashlib
import importlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tomllib


def check(source: Path, requirements: Path, tests: bool) -> dict:
    source = source.resolve()
    workspace = None
    inputs = [Path(__file__).resolve()]
    for directory in (source, *source.parents):
        manifest = directory / "pyproject.toml"
        if not (directory / "uv.lock").is_file() or not manifest.is_file():
            continue
        metadata = tomllib.loads(manifest.read_text(encoding="utf-8"))
        if "workspace" in metadata.get("tool", {}).get("uv", {}):
            workspace = directory
            inputs.extend((manifest, directory / ".python-version"))
            for member in metadata["tool"]["uv"]["workspace"]["members"]:
                inputs.extend(p / "pyproject.toml" for p in directory.glob(member))
            break
    if workspace:
        environment = workspace / ".venv"
        if Path(sys.prefix).resolve() != environment.resolve():
            raise ValueError(f"Python must come from {environment}, not {sys.prefix}")
        subprocess.run(
            [
                "uv",
                "sync",
                "--project",
                str(workspace),
                "--check",
                "--locked",
                "--offline",
                "--no-python-downloads",
                "--no-active",
            ],
            check=True,
            env={**os.environ, "UV_PROJECT_ENVIRONMENT": str(environment)},
        )
    else:
        inputs.extend(
            (
                requirements,
                requirements.with_name("build-backends.txt"),
                source / ".python-version",
            )
        )
        receipt = json.loads(
            (Path(sys.prefix) / "oxygen-build-tools.json").read_text(encoding="utf-8")
        )
        if (
            receipt["requirements_sha256"]
            != hashlib.sha256(
                requirements.read_bytes()
                + requirements.with_name("build-backends.txt").read_bytes()
            ).hexdigest()
        ):
            raise ValueError(
                "Exported build-tool requirements changed since provisioning"
            )
        for relative, digest in receipt["pyprojects"].items():
            inputs.append(source / relative)
            if digest != hashlib.sha256((source / relative).read_bytes()).hexdigest():
                raise ValueError(
                    f"Python tool metadata changed since provisioning: {relative}"
                )
        version = (source / ".python-version").read_text(encoding="utf-8").strip()
        if version != f"{sys.version_info.major}.{sys.version_info.minor}":
            raise ValueError(
                f"Exported build tools require Python {version}, not {sys.version.split()[0]}"
            )
    for name, relative in (
        ("bindless_codegen", "src/Oxygen/Core/Tools/BindlessCodeGen"),
        ("pakgen", "src/Oxygen/Cooker/Tools/PakGen"),
    ):
        module = importlib.import_module(name)
        if (
            not Path(module.__file__)
            .resolve()
            .is_relative_to((source / relative).resolve())
        ):
            raise ValueError(
                f"{name} belongs to a different checkout: {module.__file__}"
            )
        importlib.import_module(f"{name}.cli")
    for name in ("yaml", "jsonschema", *(("pytest",) if tests else ())):
        importlib.import_module(name)
    lockfile = workspace / "uv.lock" if workspace else requirements
    return {
        "lockfile": lockfile.as_posix(),
        "inputs": sorted({p.as_posix() for p in (*inputs, lockfile)}),
    }


if __name__ == "__main__":
    try:
        print(
            json.dumps(
                check(
                    Path(sys.argv[1]),
                    Path(sys.argv[2]),
                    sys.argv[3].upper() in ("ON", "TRUE", "1"),
                )
            )
        )
    except (
        OSError,
        ValueError,
        KeyError,
        ImportError,
        subprocess.CalledProcessError,
    ) as error:
        raise SystemExit(
            f"Python build tools are not ready: {error}. "
            "Run build-tree generate <profile> (or repeat conan install for an exported build)."
        )
