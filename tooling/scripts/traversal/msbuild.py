from __future__ import annotations

import json
import re
import subprocess
from pathlib import Path
from typing import Dict, Iterable, Tuple

__all__ = ["query_msbuild_properties"]


_PROPERTY_BLOCK = re.compile(r"\{\s*\"Properties\"\s*:\s*\{.*?\}\s*\}", re.DOTALL)


def query_msbuild_properties(project: Path, properties: Iterable[str], *, configuration: str | None = None) -> Tuple[Dict[str, str], subprocess.CompletedProcess[str]]:
    """Query evaluated MSBuild properties using ``dotnet build``.

    Returns a tuple with the parsed property dictionary and the completed
    process. When parsing fails, the dictionary will be empty but the raw
    process output remains available for diagnostics.
    """

    args = [
        "dotnet",
        "build",
        str(project),
        "-nologo",
        "--verbosity",
        "quiet",
    ]
    if configuration:
        args.append(f"-property:Configuration={configuration}")
    for prop in properties:
        args.append(f"--getProperty:{prop}")

    completed = subprocess.run(args, capture_output=True, text=True, check=False)
    raw_output = (completed.stdout or "") + "\n" + (completed.stderr or "")
    match = _PROPERTY_BLOCK.search(raw_output)
    if not match:
        return {}, completed

    try:
        block = match.group(0)
        payload = json.loads(block)
        props = payload.get("Properties", {})
    except json.JSONDecodeError:
        return {}, completed

    result: Dict[str, str] = {}
    for prop in properties:
        value = props.get(prop)
        if value is not None:
            result[prop] = value
    return result, completed
