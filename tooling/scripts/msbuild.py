"""Visual Studio tool discovery and evaluation without building or restoring."""
from __future__ import annotations
import json
import os
import subprocess
from functools import cache
from pathlib import Path
from typing import Iterable

@cache
def visual_studio_tool(name: str) -> Path:
    patterns = {
        "msbuild": r"MSBuild\**\Bin\amd64\MSBuild.exe",
        "vstest": r"Common7\IDE\CommonExtensions\Microsoft\TestWindow\vstest.console.exe",
    }
    installer = Path(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)"))
    vswhere = installer / "Microsoft Visual Studio/Installer/vswhere.exe"
    result = subprocess.run(
        [str(vswhere), "-latest", "-prerelease", "-products", "*", "-requires",
         "Microsoft.Component.MSBuild", "-find", patterns[name]],
        capture_output=True, text=True, check=True,
    )
    paths = [Path(line.strip()) for line in result.stdout.splitlines() if line.strip()]
    if not paths or not paths[0].is_file():
        raise RuntimeError(f"Visual Studio tool is not installed: {name}")
    return paths[0]

def query_msbuild_properties(
    project: Path, properties: Iterable[str], *, configuration: str | None = None,
    target_framework: str | None = None, runtime_identifier: str | None = None,
) -> tuple[dict[str, str], subprocess.CompletedProcess[str]]:
    # Two properties ensure MSBuild returns JSON, even for a single query.
    names = list(dict.fromkeys([*properties, "MSBuildProjectFullPath"]))
    args = [str(visual_studio_tool("msbuild")), str(project), "/nologo", "/v:quiet",
            "/getProperty:" + ",".join(names)]
    for key, value in (("Configuration", configuration), ("TargetFramework", target_framework),
                       ("RuntimeIdentifier", runtime_identifier)):
        if value:
            args.append(f"/p:{key}={value}")
    result = subprocess.run(args, capture_output=True, text=True, check=False)
    if result.returncode:
        raise RuntimeError(f"MSBuild could not evaluate {project}:\n{result.stdout}{result.stderr}")
    # Evaluation warnings may precede the JSON document.
    start = result.stdout.find("{")
    try:
        values = json.loads(result.stdout[start:])["Properties"]
    except (ValueError, KeyError) as error:
        raise RuntimeError(f"Invalid MSBuild evaluation output for {project}: {result.stdout}") from error
    return values, result
