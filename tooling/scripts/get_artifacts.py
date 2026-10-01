"""Show evaluated artifact paths; never infer output paths or build projects."""
from __future__ import annotations
import argparse
import json
import sys
from datetime import datetime
from pathlib import Path
from .msbuild import query_msbuild_properties
from .traversal.tfm import discover_target_frameworks

REPO_ROOT = Path(__file__).resolve().parents[2]
PROPERTIES = ("OutputPath", "IntermediateOutputPath", "PackageOutputPath", "PublishDir",
              "ArtifactsPath", "ArtifactsPivots", "TargetFramework", "RuntimeIdentifier")

def resolve_project(value: str) -> Path:
    path = Path(value)
    if value in (".", "./", ".\\"):
        path = Path.cwd()
    elif not path.is_absolute():
        path = REPO_ROOT / path if path.parts[0] in ("projects", "tooling", "artifacts") else REPO_ROOT / "projects" / path
    if path.is_file() and path.suffix.lower() in (".csproj", ".vcxproj"):
        return path.resolve()
    if path.is_dir():
        for directory in (path, path / "src"):
            projects = sorted([*directory.glob("*.csproj"), *directory.glob("*.vcxproj")])
            if len(projects) == 1:
                return projects[0].resolve()
            if len(projects) > 1:
                raise ValueError(f"Choose an explicit project in {directory}")
    raise FileNotFoundError(f"No project found at {path}")

def artifact_properties(project: Path, configuration: str, framework: str, rid: str | None) -> dict[str, str]:
    values, _ = query_msbuild_properties(project, PROPERTIES, configuration=configuration,
                                         target_framework=framework, runtime_identifier=rid)
    for key in PROPERTIES:
        if (key.endswith("Path") or key == "PublishDir") and values.get(key):
            path = Path(values[key])
            values[key] = str((project.parent / path).resolve()) if not path.is_absolute() else str(path.resolve())
    return {key: values.get(key, "") for key in PROPERTIES}

def run(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        prog="get-artifacts", description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""Examples:
  get-artifacts -p Oxygen.Editor -c Release
  get-artifacts -p Collections --framework-all -j
  get-artifacts -p Storage -t net9.0-windows10.0.26100.0 -l
  get-artifacts -p Oxygen.Editor.Interop/src/Oxygen.Editor.Interop.vcxproj -j

Paths may be absolute, relative to the repository, or relative to projects/.
A project directory containing one project (directly or in src/) is accepted.
Queries evaluate Visual Studio MSBuild; they never build or restore packages.
Evaluation failures return exit code 1 instead of guessed artifact locations.""",
    )
    parser.add_argument("-p", "--project", default=".", help="Project file or directory (default: current directory)")
    parser.add_argument("-c", "--configuration", default="Debug", help="Build configuration to query (default: Debug)")
    frameworks = parser.add_mutually_exclusive_group()
    frameworks.add_argument("-t", "--target-framework", help="Target framework; defaults to the first declared framework")
    parser.add_argument("-r", "--runtime-identifier", help="Override RuntimeIdentifier, for example win-x64; otherwise use the project setting")
    frameworks.add_argument("--framework-all", action="store_true", help="Query every declared framework instead of the first")
    parser.add_argument("-l", "--list", action="store_true", help="List existing output/intermediate/package files with sizes and modification times (text output only)")
    parser.add_argument("-j", "--json", action="store_true", help="Print machine-readable JSON only")
    parser.add_argument("-n", "--no-color", action="store_true", help="Use plain output (the default)")
    args = parser.parse_args(argv)
    try:
        project = resolve_project(args.project)
        frameworks = [args.target_framework] if args.target_framework else discover_target_frameworks(project, configuration=args.configuration)
        if not args.framework_all:
            frameworks = frameworks[:1]
        values = {tf: artifact_properties(project, args.configuration, tf, args.runtime_identifier) for tf in frameworks}
        output = {"Project": str(project), "Configuration": args.configuration}
        if args.framework_all:
            output.update(TargetFrameworks=frameworks, ArtifactsByFramework=values)
        else:
            output.update(values[frameworks[0]])
        if args.json:
            print(json.dumps(output, indent=2))
        else:
            print(f"{project.name} ({args.configuration})")
            for framework, properties in values.items():
                print(framework)
                for key, value in properties.items():
                    print(f"  {key}: {value}")
                if args.list:
                    for key in ("OutputPath", "IntermediateOutputPath", "PackageOutputPath"):
                        if not properties[key]:
                            continue
                        for file in sorted(Path(properties[key]).rglob("*")):
                            if file.is_file():
                                info = file.stat()
                                print(f"  {datetime.fromtimestamp(info.st_mtime):%Y-%m-%d %H:%M:%S} {info.st_size:>10} {file}")
        return 0
    except (OSError, RuntimeError, ValueError) as error:
        print(str(error), file=sys.stderr)
        return 1

if __name__ == "__main__":
    raise SystemExit(run())
