"""Command line interface for PakGen (stub)."""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

from .logging import configure_logging, step
from .reporting import (
    set_reporter,
    PlainReporter,
    JsonLinesReporter,
    SilentReporter,
    RichReporter,
    set_verbosity,
)
from .diff import diff_paks_deep
import json
from .api import plan_dry_run, build_pak, BuildOptions


def _build_cmd(args: argparse.Namespace) -> int:
    opts = BuildOptions(
        input_spec=args.spec,
        output_path=args.output,
        manifest_path=args.emit_manifest,
        deterministic=args.deterministic,
        simulate_material_delay=getattr(args, "simulate_material_delay", None),
    )
    build_pak(opts)
    return 0


def _inspect_cmd(args: argparse.Namespace) -> int:
    step("inspecting pak (stub)")
    return 0


def _validate_cmd(args: argparse.Namespace) -> int:
    step("validating spec (stub)")
    return 0


def _diff_cmd(args: argparse.Namespace) -> int:
    step("diffing pak files")
    result = diff_paks_deep(args.left, args.right)
    from .reporting import get_reporter

    rep = get_reporter()
    rep.section("Diff results")
    summary = result.get("summary", {})
    diff_count = summary.get("count")
    rep.status(
        "Diff summary: count="
        + f"{diff_count} left={args.left.name} right={args.right.name}",
    )
    # Still emit JSON for full machine-readable diff to stdout
    print(json.dumps(result, indent=2, sort_keys=True))
    return 1 if diff_count else 0


def _plan_cmd(args: argparse.Namespace) -> int:
    plan, plan_dict = plan_dry_run(
        args.spec,
        deterministic=args.deterministic,
        simulate_material_delay=getattr(args, "simulate_material_delay", None),
    )
    # Ensure any active progress UI is finalized before emitting output
    from .reporting import get_reporter

    rep = get_reporter()
    try:
        rep.flush()
    except Exception:  # pragma: no cover - defensive
        pass
    if args.json:
        print(json.dumps(plan_dict, indent=2, sort_keys=True))
    else:
        # Minimal human summary via reporter
        regions_summary = ",".join(
            f"{r.name}@{r.offset}+{r.size}" for r in plan.regions if r.size
        )
        rep.status(
            f"Plan summary: file_size={plan.file_size} regions={regions_summary}",
        )
    return 0


def build_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(
        prog="pakgen", description="Pak file generation tool"
    )
    p.add_argument(
        "-v",
        "--verbose",
        action="count",
        default=0,
        help="Increase verbosity (repeatable)",
    )
    # Unified reporter selection.
    p.add_argument(
        "-r",
        "--reporter",
        choices=["plain", "rich", "json", "silent"],
        default="plain",
        help="Select reporter backend: plain (default), rich, json (JSONL events), silent",
    )
    sub = p.add_subparsers(dest="cmd", required=True)

    b = sub.add_parser("build", help="Build a pak from a spec file")
    b.add_argument("spec", type=Path)
    b.add_argument("output", type=Path)
    b.add_argument(
        "--emit-manifest",
        dest="emit_manifest",
        type=Path,
        help="Optional path to write manifest JSON (opt-in)",
    )
    b.add_argument(
        "--deterministic",
        action="store_true",
        help="Enable deterministic ordering / layout (stable hash)",
    )
    b.add_argument(
        "--simulate-material-delay",
        type=float,
        dest="simulate_material_delay",
        help="Testing: add per-material processing delay (seconds)",
    )
    b.set_defaults(func=_build_cmd)

    i = sub.add_parser("inspect", help="Inspect a pak file")
    i.add_argument("pak", type=Path)
    i.set_defaults(func=_inspect_cmd)

    v = sub.add_parser("validate", help="Validate a spec file")
    v.add_argument("spec", type=Path)
    v.set_defaults(func=_validate_cmd)

    d = sub.add_parser("diff", help="Diff two pak files")
    d.add_argument("left", type=Path)
    d.add_argument("right", type=Path)
    d.set_defaults(func=_diff_cmd)

    pl = sub.add_parser("plan", help="Compute plan (dry run, no write)")
    pl.add_argument("spec", type=Path)
    pl.add_argument("--json", action="store_true", help="Emit JSON plan")
    pl.add_argument(
        "--deterministic",
        action="store_true",
        help="Enable deterministic ordering policies",
    )
    pl.add_argument(
        "--simulate-material-delay",
        type=float,
        dest="simulate_material_delay",
        help="Testing: add per-material processing delay (seconds)",
    )
    pl.set_defaults(func=_plan_cmd)

    return p


def main(argv: list[str] | None = None) -> int:
    argv = argv if argv is not None else sys.argv[1:]
    parser = build_parser()
    args = parser.parse_args(argv)
    # Reporter selection based solely on --reporter.
    requested = args.reporter
    if requested == "json":
        set_reporter(JsonLinesReporter())
    elif requested == "silent":
        set_reporter(SilentReporter())
    elif requested == "rich":
        if sys.stderr.isatty():
            try:
                set_reporter(RichReporter())
            except Exception:  # pragma: no cover
                set_reporter(PlainReporter())
        else:
            # Fallback quietly to plain if no TTY
            set_reporter(PlainReporter())
    else:  # plain
        set_reporter(PlainReporter())
    # Apply verbosity globally for reporters (verbose gating)
    set_verbosity(args.verbose)
    configure_logging(args.verbose)
    return args.func(args)


if __name__ == "__main__":  # pragma: no cover
    raise SystemExit(main())
