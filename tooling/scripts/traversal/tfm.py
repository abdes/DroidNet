from __future__ import annotations

from pathlib import Path
from typing import List

from .msbuild import query_msbuild_properties

__all__ = ["discover_target_frameworks"]


def discover_target_frameworks(project: Path, *, configuration: str | None = None) -> List[str]:
    """Return the evaluated target frameworks for the project.

    The function requests both ``TargetFrameworks`` (multi-targeting) and
    ``TargetFramework`` (single) using the MSBuild property querying support in
    ``dotnet build``. When both properties are empty, the function raises a
    ``RuntimeError`` so callers can decide on a fallback strategy.
    """

    properties = ["TargetFrameworks", "TargetFramework"]
    values, completed = query_msbuild_properties(project, properties, configuration=configuration)

    target_frameworks: List[str] = []
    multi = values.get("TargetFrameworks")
    if multi:
        target_frameworks.extend([
            part.strip() for part in multi.split(";") if part.strip()
        ])

    single = values.get("TargetFramework")
    if single:
        tfm = single.strip()
        if tfm and tfm not in target_frameworks:
            target_frameworks.append(tfm)

    if target_frameworks:
        return target_frameworks

    if completed.returncode != 0:
        raise RuntimeError(
            f"dotnet build returned exit code {completed.returncode} when evaluating TargetFrameworks"
        )

    raise RuntimeError("Unable to determine TargetFramework from MSBuild properties")
