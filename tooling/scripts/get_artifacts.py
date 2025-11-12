#!/usr/bin/env python3
"""
Helper to query artifact locations and list built artifacts for .NET projects.
Supports .NET SDK 8+ artifacts output layout (UseArtifactsOutput) and falls back to the computed path layout.

You can run this helper as a script or as a module:
    python -m tooling.scripts.get_artifacts --project <csproj> --configuration Debug --json

Optional flags:
    --project <path>            Path to csproj or folder containing single csproj (default '.' uses cwd)
    --configuration <name>      e.g. Debug/Release (default Debug)
    --target-framework <tfm>    Optional target framework (net9.0, net9.0-windows10.0.26100.0)
    --runtime-identifier <rid>  Optional runtime identifier (win-x64)
    --list                      List files found beneath output/obj/package directories
    --framework-all              When a project specifies multiple TFMs, query all of them and provide per-TFM results
    --json                      Print a JSON object with properties (when specified, only JSON is printed)

The helper prefers querying MSBuild for ArtifactsPivots/OutputPath/ArtifactsPath; it only computes fallback paths when the SDK doesn't provide them.
"""
import argparse
import json
from datetime import datetime
import os
import re
import subprocess
import sys
from pathlib import Path
from typing import Dict, Optional

# Optional rich console for colorful output. We import lazily so the script
# continues to work even when rich isn't installed in the environment.
try:
    from rich.console import Console
    from rich.markup import escape as rich_escape
    _RICH_CONSOLE = Console()
except Exception:
    _RICH_CONSOLE = None
    rich_escape = None

# Create safe escape/print utilities so type checkers are happy and we can
# use a fallback when rich isn't installed.
if _RICH_CONSOLE and rich_escape:
    def _safe_escape(s: str) -> str:
        return rich_escape(s) # type: ignore
else:
    def _safe_escape(s: str) -> str:
        return s

def _console_print(msg: str) -> None:
    if _RICH_CONSOLE:
        _RICH_CONSOLE.print(msg)
    else:
        print(msg)

SCRIPT_ROOT = Path(__file__).resolve().parent

def _detect_repo_root_git(start: Path) -> Optional[Path]:
    # Prefer using git to detect the repository root for accuracy.
    try:
        # Use git from the start dir to find the repo root. This returns an absolute path.
        proc = subprocess.run(["git", "-C", str(start), "rev-parse", "--show-toplevel"], capture_output=True, text=True)
        if proc.returncode == 0 and proc.stdout:
            return Path(proc.stdout.strip())
    except Exception:
        pass
    return None


def _detect_repo_root_heuristic(start: Path) -> Path:
    # Walk up until we detect a repo root containing 'projects' or a solution file.
    cur = start
    while True:
        if (cur / 'projects').exists() and (cur / 'projects').is_dir():
            return cur
        if cur.parent == cur:
            break
        cur = cur.parent
    # fallback to two levels up (matches previous assumption) if detection fails
    return start.parents[1]


repo_root_git = _detect_repo_root_git(SCRIPT_ROOT)
if repo_root_git:
    REPO_ROOT = repo_root_git
else:
    REPO_ROOT = _detect_repo_root_heuristic(SCRIPT_ROOT)


def run_dotnet_get_properties(project: Path, configuration: str, target_framework: Optional[str], rid: Optional[str]):
    args = [
        "dotnet",
        "build",
        str(project),
        f"-property:Configuration={configuration}",
        "--getProperty:OutputPath",
        "--getProperty:TargetFrameworks",
        "--getProperty:TargetFramework",
        "--getProperty:ArtifactsPath",
        "--getProperty:ArtifactsPivots",
        "--getProperty:IntermediateOutputPath",
        "--getProperty:PackageOutputPath",
        "--verbosity",
        "minimal",
    ]
    if target_framework:
        args.append(f"-property:TargetFramework={target_framework}")
    if rid:
        args.append(f"-property:RuntimeIdentifier={rid}")

    # Execute dotnet and collect combined output
    process = subprocess.run(args, capture_output=True, text=True)
    stdout = (process.stdout or "") + "\n" + (process.stderr or "")
    code = process.returncode
    return code, stdout


def parse_dotnet_json(stdout: str) -> Optional[Dict[str, str]]:
    # Extract the JSON object block from stdout -- dotnet returns JSON for --getProperty
    # This is robust to occasional extra lines in the output.
    match = re.search(r"\{\s*\"Properties\"\s*:\s*\{.*?\}\s*\}", stdout, re.DOTALL)
    if not match:
        return None

    try:
        block = match.group(0)
        data = json.loads(block)
        props = data.get("Properties", {})
        return {k: props.get(k) for k in ("OutputPath", "ArtifactsPath", "ArtifactsPivots", "IntermediateOutputPath", "PackageOutputPath")}
    except Exception:
        return None


def compute_artifacts_layout(project: Path, configuration: str, target_framework: Optional[str], rid: Optional[str]) -> Dict[str, str]:
    # Determine project name from csproj file
    project_name = project.stem
    pivots = configuration
    # When both TargetFramework and RID are present, use a hyphen between TFM and RID
    # so it matches the MSBuild artifacts pivot format like Debug_net9.0-windows10.0.26100.0
    if target_framework and rid:
        # If the target framework already contains a hyphen, treat it as combined TFM+RID
        if "-" in target_framework:
            pivots = f"{pivots}_{target_framework}"
        else:
            pivots = f"{pivots}_{target_framework}-{rid}"
    elif target_framework:
        pivots = f"{pivots}_{target_framework}"
    elif rid:
        pivots = f"{pivots}_{rid}"

    artifacts_root = str((REPO_ROOT / "artifacts").resolve())
    output_path = str(Path(artifacts_root) / "bin" / project_name / pivots)
    intermediate_path = str(Path(artifacts_root) / "obj" / project_name / pivots)
    package_path = str(Path(artifacts_root) / "package" / configuration)
    return {
        "OutputPath": output_path,
        "ArtifactsPath": artifacts_root,
        "IntermediateOutputPath": intermediate_path,
        "PackageOutputPath": package_path,
    }


def list_files_under(path: Optional[str]):
    if not path:
        return []
    p = Path(path)
    if not p.exists():
        return []
    result = []
    for f in p.rglob("*"):
        if f.is_file():
            stat = f.stat()
            result.append({"path": str(f), "size": stat.st_size, "mtime": stat.st_mtime})
    return result


def parse_args():
    parser = argparse.ArgumentParser(prog="get_artifacts.py")
    parser.add_argument("-p", "--project", dest="project", default='.', help="Path to .csproj file or a folder containing a single csproj")
    parser.add_argument("-c", "--configuration", dest="configuration", default="Debug")
    parser.add_argument("-t", "--target-framework", dest="target_framework", default=None)
    parser.add_argument("--framework-all", dest="framework_all", action="store_true", help="When project targets multiple frameworks, query all frameworks and return per-framework results")
    parser.add_argument("-r", "--runtime-identifier", dest="runtime_identifier", default=None)
    parser.add_argument("-l", "--list", dest="list", action="store_true")
    parser.add_argument("-j", "--json", dest="json", action="store_true")
    parser.add_argument("-n", "--no-color", dest="no_color", action="store_true", help="Disable colorized output even if 'rich' is installed")
    return parser.parse_args()


def _find_csproj_direct_in_dir(dir_path: Path):
    return list(dir_path.glob("*.csproj"))


def _find_csproj_under_src(dir_path: Path):
    src_dir = dir_path / 'src'
    if not src_dir.exists() or not src_dir.is_dir():
        return []
    return list(src_dir.rglob('*.csproj'))


def _choose_single_csproj(candidates: list[Path], dir_name: str) -> Optional[Path]:
    if not candidates:
        return None
    if len(candidates) == 1:
        return candidates[0]
    # Prefer csproj that matches the directory name if possible
    for c in candidates:
        if c.stem.lower() == dir_name.lower():
            return c
    # fallback: return the first candidate
    return candidates[0]


def _resolve_project_path(args, project_input: Path, _repo_root: Path) -> Optional[Path]:
    # Resolve repository-root-relative heuristics
    if str(project_input) in ('.', './', '.\\'):
        project_input = Path.cwd()
    elif not project_input.is_absolute():
        p0 = str(project_input).replace('/', os.sep).replace('\\', os.sep)
        first = p0.split(os.sep)[0] if p0 else ''
        if first.lower() == 'projects':
            project_input = (_repo_root / project_input).resolve()
        else:
            project_input = (_repo_root / 'projects' / project_input).resolve()

    # If a file was provided and it's a csproj, return it
    if project_input.is_file() and project_input.suffix.lower() == '.csproj':
        return project_input
    # If it's a directory, search for csproj directly in dir
    if project_input.is_dir():
        cs = _find_csproj_direct_in_dir(project_input)
        if len(cs) == 1:
            return cs[0]
        if len(cs) > 1:
            # If multiple csproj files are present, prefer one matching the folder name
            chosen = _choose_single_csproj(cs, project_input.name)
            return chosen
        # Fallback: search under src/ subdirectory
        cs_src = _find_csproj_under_src(project_input)
        chosen = _choose_single_csproj(cs_src, project_input.name)
        return chosen
    # Not found, return None
    return None


def _get_artifact_properties(project: Path, configuration: str, tf: Optional[str], rid: Optional[str]) -> Dict[str, str]:
    code, stdout = run_dotnet_get_properties(project, configuration, tf, rid)
    parsed = parse_dotnet_json(stdout) if code == 0 else None
    if parsed is None:
        parsed = compute_artifacts_layout(project, configuration, tf, rid)
    # Normalize path separators
    for k, v in parsed.items():
        if v is not None:
            parsed[k] = os.path.normpath(v)
    return parsed


def run():
    parser = argparse.ArgumentParser(prog="get_artifacts.py")
    parser.add_argument("-p", "--project", dest="project", default='.', help="Path to .csproj file or a folder containing a single csproj")
    parser.add_argument("-c", "--configuration", dest="configuration", default="Debug")
    parser.add_argument("-t", "--target-framework", dest="target_framework", default=None)
    parser.add_argument("--framework-all", dest="framework_all", action="store_true", help="When project targets multiple frameworks, query all frameworks and return per-framework results")
    parser.add_argument("-r", "--runtime-identifier", dest="runtime_identifier", default=None)
    parser.add_argument("-l", "--list", dest="list", action="store_true")
    parser.add_argument("-j", "--json", dest="json", action="store_true")
    parser.add_argument("-n", "--no-color", dest="no_color", action="store_true", help="Disable colorized output even if 'rich' is installed")

    args = parse_args()

    project_input = Path(args.project)
    resolved_project = _resolve_project_path(args, project_input, REPO_ROOT)
    if not resolved_project:
        print(f"No csproj found in directory {project_input}", file=sys.stderr)
        return 2
    project = resolved_project
    # Preserve legacy behavior when project is '.' (current dir)
    if str(project_input) in ('.', './', '.\\'):
        project_input = Path.cwd()
    else:
        # Map relative project paths to repo-root/projects/<path> unless it already
        # begins with 'projects' or is absolute. This lets you pass short
        # paths like `Mvvm.Generators/src/...` and have them resolved automatically.
        if not project_input.is_absolute():
            p0 = str(project_input).replace('/', os.sep).replace('\\', os.sep)
            first = p0.split(os.sep)[0] if p0 else ''
            if first.lower() == 'projects':
                project_input = (REPO_ROOT / project_input).resolve()
            else:
                project_input = (REPO_ROOT / 'projects' / project_input).resolve()
    # Map relative project paths to repo-root/projects/<path> unless the supplied
    # relative path already begins with 'projects'. This lets you pass short
    # paths like `Mvvm.Generators/src/...` and have them resolved automatically.
    if not project_input.is_absolute():
        p0 = str(project_input).replace('/', os.sep).replace('\\', os.sep)
        first = p0.split(os.sep)[0] if p0 else ''
        if first.lower() == 'projects':
            project_input = (REPO_ROOT / project_input).resolve()
        else:
            project_input = (REPO_ROOT / 'projects' / project_input).resolve()
    if not project.exists():
        print(f"Project does not exist: {project}", file=sys.stderr)
        return 2

    # If no target framework was provided, check if the project defines TargetFrameworks (multi-targeting)
    if not args.target_framework:
        tf_code, tf_stdout = run_dotnet_get_properties(project, args.configuration, None, None)
        # Query TargetFrameworks property directly
        tf_match = re.search(r'"TargetFrameworks"\s*:\s*"([^"]+)"', tf_stdout)
        if not tf_match:
            # Also try TargetFramework (single value in some projects)
            tf_match = re.search(r'"TargetFramework"\s*:\s*"([^"]+)"', tf_stdout)
        target_frameworks = []
        if tf_match:
            target_frameworks = [x.strip() for x in tf_match.group(1).split(';') if x.strip()]
        if args.framework_all and target_frameworks:
            # Collect results for each TFM
            results = {}
            for tf in target_frameworks:
                code, stdout = run_dotnet_get_properties(project, args.configuration, tf, args.runtime_identifier)
                parsed = parse_dotnet_json(stdout) if code == 0 else None
                if not parsed:
                    parsed = compute_artifacts_layout(project, args.configuration, tf, args.runtime_identifier)
                # Normalize
                for k, v in parsed.items():
                    if v is not None:
                        parsed[k] = os.path.normpath(v)
                results[tf] = parsed
            # Output combined results
            out = {
                "Project": str(project.resolve()),
                "Configuration": args.configuration,
                "TargetFrameworks": target_frameworks,
                "ArtifactsByFramework": results,
            }
            if args.json:
                print(json.dumps(out, indent=2, ensure_ascii=False))
            else:
                print(json.dumps(out, indent=2, ensure_ascii=False))
            return 0
        if target_frameworks:
            # default behavior: pick the first TFM if not requesting all
            args.target_framework = target_frameworks[0]

    parsed = _get_artifact_properties(project, args.configuration, args.target_framework, args.runtime_identifier)

    # Normalize trailing separators
    for k, v in parsed.items():
        if v is not None:
            parsed[k] = os.path.normpath(v)

    out = {
        "Project": str(project.resolve()),
        "Configuration": args.configuration,
        "TargetFramework": args.target_framework or "",
        "RuntimeIdentifier": args.runtime_identifier or "",
        "ArtifactsPivots": parsed.get("ArtifactsPivots"),
        "OutputPath": parsed.get("OutputPath"),
        "ArtifactsPath": parsed.get("ArtifactsPath"),
        "IntermediateOutputPath": parsed.get("IntermediateOutputPath"),
        "PackageOutputPath": parsed.get("PackageOutputPath"),
    }

    # Decide whether to use rich/colored output: prefer color when rich is available
    # unless --no-color is specified.
    use_rich = _RICH_CONSOLE and not args.no_color

    # Helper to print informational output; prints unless --json is requested.
    def _iprint(msg: str, style: Optional[str] = None):
        # silence human-readable output when --json is requested
        if args.json:
            return
        if use_rich:
            if style:
                _RICH_CONSOLE.print(msg, style=style) # type: ignore
            else:
                _RICH_CONSOLE.print(msg) # type: ignore
        else:
            print(msg)

    # default label width; we'll recompute based on actual labels below
    label_width: int = 18

    def _format_mtime(ts: float) -> str:
        try:
            # msbuild returns epoch in seconds
            return datetime.fromtimestamp(float(ts)).strftime("%Y-%m-%d %H:%M:%S")
        except Exception:
            return str(ts)

    def _print_file_table(files: list, title: str, args, use_rich, _iprint_fn):
        # files: list of {path,size,mtime}
        if not files:
            _iprint(f"{title}: (no files)", style="yellow")
            return
        # When JSON is requested, do not print formatted tables
        if args.json:
            return
        # Colored rich table when available
        if use_rich:
            from rich.table import Table
            # Prefer a table without borders for cleaner output; fall back to a minimal edge-less table
            try:
                from rich import box as _box
                table = Table(show_header=True, header_style="bold magenta", box=None, show_edge=False)
            except Exception:
                table = Table(show_header=True, header_style="bold magenta", show_edge=False)
            table.add_column("Time", style="cyan")
            table.add_column("Size", justify="right")
            table.add_column("Path")
            for entry in files:
                time_str = _format_mtime(entry.get("mtime")) if entry.get("mtime") is not None else ""
                table.add_row(_safe_escape(time_str), str(entry["size"]), _safe_escape(str(entry["path"])))
            _RICH_CONSOLE.print(f"[bold cyan]{_safe_escape(title)}[/bold cyan]") # pyright: ignore[reportOptionalMemberAccess]
            _RICH_CONSOLE.print() # type: ignore
            _RICH_CONSOLE.print(table) # type: ignore
            _RICH_CONSOLE.print() # type: ignore
            return
        # Plain aligned output
        # Columns: Time (19), Size (right-aligned), Path
        lines = []
        for e in files:
            time_str = _format_mtime(e.get("mtime")) if e.get("mtime") is not None else ""
            lines.append((time_str, e["size"], e["path"]))
        size_width = max((len(str(s)) for _, s, _ in lines), default=4)
        time_width = 19
        _iprint_fn(f"\n{title}", style="cyan")
        _iprint_fn(f"{'Time'.ljust(time_width)} {'Size'.rjust(size_width)}  Path", style="bold")
        for t, s, p in lines:
            _iprint_fn(f"{t.ljust(time_width)} {str(s).rjust(size_width)}  {p}")
        # blank line after plain file table
        _iprint_fn("")

    if args.list:
        out_files = {"Bin": list_files_under(out["OutputPath"]), "Obj": list_files_under(out["IntermediateOutputPath"]), "Packages": list_files_under(out["PackageOutputPath"]) }
        out["Files"] = out_files

    # compute label width based on the fields we will print
    fields = [
        "Project",
        "Configuration",
        "TargetFramework",
        "RuntimeIdentifier",
        "ArtifactsRoot",
        "OutputPath",
        "IntermediateOutputPath",
        "PackageOutputPath",
    ]
    label_width = max((len(f) for f in fields), default=label_width)

    # If we don't have an explicit OutputPath but we have ArtifactsPivots and ArtifactsPath, compute the expected path
    if not out.get("OutputPath") and out.get("ArtifactsPivots") and out.get("ArtifactsPath"):
        out["OutputPath"] = os.path.normpath(os.path.join(str(out["ArtifactsPath"]), "bin", str(Path(project).stem), str(out.get("ArtifactsPivots"))))
        out["IntermediateOutputPath"] = os.path.normpath(os.path.join(str(out["ArtifactsPath"]), "obj", str(Path(project).stem), str(out.get("ArtifactsPivots"))))
        out["PackageOutputPath"] = os.path.normpath(os.path.join(str(out["ArtifactsPath"]), "package", str(out.get("Configuration"))))

    if args.json:
        print(json.dumps(out, indent=2, ensure_ascii=False))
    else:
        # Print metadata fields with consistent formatting using a block so alignment is consistent
        def _print_metadata_block():
            pairs = [
                ("Project", out['Project']),
                ("Configuration", out['Configuration']),
                ("TargetFramework", out['TargetFramework']),
                ("RuntimeIdentifier", out['RuntimeIdentifier']),
                ("ArtifactsRoot", out['ArtifactsPath']),
                ("OutputPath", out['OutputPath']),
                ("IntermediateOutputPath", out['IntermediateOutputPath']),
                ("PackageOutputPath", out['PackageOutputPath']),
            ]
            if args.json:
                return
            if use_rich:
                from rich.table import Table
                try:
                    from rich import box as _box
                    # Use a borderless table for metadata to match the file table style used above
                    table = Table(show_header=False, box=None, show_edge=False)
                except Exception:
                    table = Table(show_header=False, show_edge=False)
                table.add_column("label", justify="right", style="bold green", width=label_width)
                table.add_column("value")
                for label, value in pairs:
                    if value is None or value == "":
                        continue
                    table.add_row(label, _safe_escape(value))
                _RICH_CONSOLE.print(table) # type: ignore
                _RICH_CONSOLE.print() # type: ignore
            else:
                # Print same borderless layout as rich but without colors
                for label, value in pairs:
                    if value is None or value == "":
                        continue
                    print(f"{label.rjust(label_width)}  {_safe_escape(value)}")

        print()
        _print_metadata_block()

    if args.list and not args.json:
        _print_file_table(out["Files"]["Bin"], f"Files under OutputPath ({len(out['Files']['Bin'])})", args, use_rich, _iprint)
        _print_file_table(out["Files"]["Obj"], f"Files under IntermediateOutputPath ({len(out['Files']['Obj'])})", args, use_rich, _iprint)
        _print_file_table(out["Files"]["Packages"], f"Packages ({len(out['Files']['Packages'])})", args, use_rich, _iprint)

    return 0

if __name__ == '__main__':
    sys.exit(run())
