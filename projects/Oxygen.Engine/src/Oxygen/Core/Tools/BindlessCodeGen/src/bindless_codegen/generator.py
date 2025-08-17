# ===-----------------------------------------------------------------------===#
# Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
# copy at https://opensource.org/licenses/BSD-3-Clause.
# SPDX-License-Identifier: BSD-3-Clause
# ===-----------------------------------------------------------------------===#

"""Generator orchestration: load YAML, normalize, validate, render, write."""

import yaml
import time
import os
import json
from pathlib import Path
import importlib
import shutil

try:
    # Normal package import when installed or run as package
    from . import heaps as heaps_module
except Exception:
    # Fallback: load heaps.py directly from same directory when generator is
    # executed as a standalone module via importlib.util.spec_from_file_location
    import importlib.util
    import sys

    heaps_path = Path(__file__).resolve().parent / "heaps.py"
    spec_h = importlib.util.spec_from_file_location(
        "bindless_codegen.heaps", str(heaps_path)
    )
    if spec_h is None or spec_h.loader is None:
        raise ImportError(f"Failed to load heaps module from {heaps_path}")
    heaps_module = importlib.util.module_from_spec(spec_h)
    spec_h.loader.exec_module(heaps_module)
    # register it so other imports can find it by name
    sys.modules["bindless_codegen.heaps"] = heaps_module

from . import schema as schema_mod
from . import domains as domains_mod
from . import root_signature as rs_mod
from . import model as model_mod
from .templates import (
    TEMPLATE_CPP,
    TEMPLATE_HLSL,
    TEMPLATE_RS_CPP,
    TEMPLATE_HEAPS_D3D12_CPP,
    TEMPLATE_META_CPP,
)
from ._version import __version__ as TOOL_VERSION
import re


def render_cpp_domains(domains):
    return domains_mod.render_cpp_domains(domains)


def render_hlsl_domains(domains):
    return domains_mod.render_hlsl_domains(domains)


def _render_root_sig_cpp(root_sig: list[dict]) -> tuple[str, str, str]:
    """Render pieces for the RootSignature C++ header.

    Returns (root_param_enums, root_constants_counts, register_space_consts)
    """
    # Root parameter enum entries preserving order and indices
    enum_lines: list[str] = []
    counts_lines: list[str] = []
    regs_lines: list[str] = []

    def _norm(name: str) -> str:
        # CamelCase to UpperCamel with acronym normalization for C++ identifiers
        return domains_mod._normalize_acronyms(name)

    for idx, p in enumerate(root_sig or []):
        name = p.get("name") or f"Param{idx}"
        enum_lines.append(f"  k{_norm(str(name))} = {idx},")
        if p.get("type") == "root_constants":
            n = int(p.get("num_32bit_values", 0))
            counts_lines.append(
                f"static constexpr uint32_t k{_norm(str(name))}ConstantsCount = {n}u;"
            )
        # Record register/space bindings when available for convenience
        reg = p.get("shader_register") or p.get("base_shader_register")
        space = p.get("register_space")
        if reg is not None:
            # Extract the numeric and the letter prefix (b/t/s/u)
            sreg = str(reg)
            letter = (
                "".join([c for c in sreg if c.isalpha()])[:1].lower() or "r"
            )
            digits = "".join([c for c in sreg if c.isdigit()]) or "0"
            regs_lines.append(
                f"static constexpr uint32_t k{_norm(str(name))}Register = {int(digits)}u; // '{sreg}'"
            )
        if space is not None:
            sspace = str(space)
            digits = "".join([c for c in sspace if c.isdigit()]) or "0"
            regs_lines.append(
                f"static constexpr uint32_t k{_norm(str(name))}Space = {int(digits)}u; // '{sspace}'"
            )

    # Add Count enumerator
    enum_lines.append("  kCount = " + str(len(root_sig or [])) + ",")
    return (
        "\n".join(enum_lines),
        "\n".join(counts_lines) or "// none",
        "\n".join(regs_lines) or "// none",
    )


def atomic_write(path, content):
    tmp = path + ".tmp"
    with open(tmp, "w", encoding="utf-8") as f:
        f.write(content)
    if os.path.exists(path):
        with open(path, "r", encoding="utf-8") as f:
            old = f.read()
        if old == content:
            os.remove(tmp)
            return False
    os.replace(tmp, path)
    return True


def transactional_write_files(files: dict[str, str]) -> bool:
    """Write multiple files atomically as a single transaction.

    - Writes each content to a temp file first.
    - Determines which targets actually change (content diff).
    - For changed targets, backs up originals (if any) and replaces them.
    - If any replacement fails, rolls back all replacements and leaves
      originals intact. Temp files are cleaned up.

    Returns True if any target changed content, False otherwise.
    Raises RuntimeError on failure (after rollback).
    """
    tmp_suffix = ".tmp.tx"
    bak_suffix = ".bak.tx"
    wrote_tmps: list[tuple[str, str]] = []  # (target, tmp)
    to_replace: list[str] = []
    changed_any = False

    try:
        # Stage: write temp files and decide which files will change
        for target, content in files.items():
            tmp = target + tmp_suffix
            with open(tmp, "w", encoding="utf-8") as f:
                f.write(content)
            wrote_tmps.append((target, tmp))
            # Compare with existing content
            same = False
            if os.path.exists(target):
                with open(target, "r", encoding="utf-8") as rf:
                    old = rf.read()
                same = old == content
            if not same:
                to_replace.append(target)
                changed_any = True

        if not to_replace:
            # Nothing to do; remove tmps and return
            for _, tmp in wrote_tmps:
                try:
                    os.remove(tmp)
                except OSError:
                    pass
            return False

        # Replace stage with backups
        replaced: list[str] = []
        backups: dict[str, str] = {}
        created_new: set[str] = set()
        for target, tmp in wrote_tmps:
            if target not in to_replace:
                # No change; just drop tmp
                try:
                    os.remove(tmp)
                except OSError:
                    pass
                continue
            # Backup existing if present
            bak = target + bak_suffix
            if os.path.exists(target):
                shutil.copyfile(target, bak)
                backups[target] = bak
            else:
                created_new.add(target)
            # Replace
            os.replace(tmp, target)
            replaced.append(target)

        # Success: cleanup backups
        for target, bak in backups.items():
            try:
                os.remove(bak)
            except OSError:
                pass
        return changed_any

    except Exception as e:
        # Rollback
        try:
            for target in to_replace if "to_replace" in locals() else []:
                bak = (
                    (target + bak_suffix)
                    if os.path.exists(target + bak_suffix)
                    else None
                )
                if target in (r for r in locals().get("replaced", [])):
                    # If original existed, restore from backup; else remove created file
                    if bak and os.path.exists(bak):
                        os.replace(bak, target)
                    elif target in locals().get("created_new", set()):
                        try:
                            os.remove(target)
                        except OSError:
                            pass
            # Cleanup remaining tmps
            for _, tmp in locals().get("wrote_tmps", []):
                try:
                    os.remove(tmp)
                except OSError:
                    pass
        finally:
            raise RuntimeError(f"Transactional write failed: {e}") from e


def validate_input(doc, schema_path=None):
    """Normalize and validate the document against the JSON schema."""
    schema_mod.normalize_doc(doc)
    return schema_mod.validate_against_schema(doc, schema_path)


def find_schema(explicit_path: str | None, script_path: str) -> Path | None:
    return schema_mod.find_schema(explicit_path, script_path)


def validate_domain_references(doc):
    """Cross-validate domains vs root_signature using specialized modules."""
    domains = doc.get("domains", [])
    root_sig = doc.get("root_signature", [])
    domains_mod.validate_root_table_references(domains, root_sig)
    domains_mod.validate_domain_overlaps_per_root_table(domains)
    rs_mod.validate_params_vs_domains(
        root_sig, {d.get("id"): d for d in domains}
    )


def generate(
    input_yaml,
    out_cpp=None,
    out_hlsl=None,
    *,
    dry_run: bool = False,
    schema_path: str | None = None,
    out_base: str | None = None,
    reporter=None,
):
    rep = reporter
    if rep is None:
        # Lazy import to avoid circulars
        try:
            from .reporting import Reporter

            rep = Reporter()
        except Exception:  # pragma: no cover - fallback

            class _Noop:
                def error(self, *a, **k):
                    pass

                def warn(self, *a, **k):
                    pass

                def info(self, *a, **k):
                    pass

                def progress(self, *a, **k):
                    pass

                def debug(self, *a, **k):
                    pass

            rep = _Noop()

    rep.info("BindlessCodeGen %s", TOOL_VERSION)
    # Determine a base directory for concise path printing
    try:
        if out_base:
            base_dir = os.path.abspath(os.path.dirname(out_base))
        elif out_cpp:
            base_dir = os.path.abspath(os.path.dirname(out_cpp))
        else:
            base_dir = os.path.abspath(os.getcwd())
    except Exception:
        base_dir = os.path.abspath(os.getcwd())

    def _short(p: str) -> str:
        try:
            return os.path.relpath(p, base_dir)
        except Exception:
            return p

    rep.info("base: %s", base_dir)
    rep.progress("Loading input YAML: %s", _short(input_yaml))
    with open(input_yaml, "r", encoding="utf-8") as f:
        doc = yaml.safe_load(f)
    # Normalize + schema validation
    schema_label = schema_path or "auto"
    try:
        if schema_path:
            schema_label = _short(schema_path)
    except Exception:
        pass
    rep.progress("Resolving schema (%s)", schema_label)
    resolved = None
    try:
        # Prefer a schema adjacent to the input YAML
        sib = Path(input_yaml).with_name("Spec.schema.json")
        if sib.exists():
            resolved = sib
    except Exception:
        resolved = None
    if resolved is None:
        resolved = find_schema(schema_path, __file__)
    if resolved is None:
        rep.info("Schema: not found (skipping)")
    else:
        rep.info("Schema: %s", _short(str(resolved)))
    # If available, read schema version hint (x-oxygen-schema-version)
    schema_version = None
    try:
        if resolved is not None:
            with open(resolved, "r", encoding="utf-8") as sf:
                schema_doc = json.load(sf)
            schema_version = schema_doc.get("x-oxygen-schema-version")
            if schema_version:
                rep.info("Schema version: %s", schema_version)
    except Exception:
        pass
    rep.progress("Normalizing + validating document")
    validate_input(doc, resolved)
    # Enforce semantic versioning presence on spec (meta.version)
    meta = doc.get("meta", {}) or {}
    src_ver = meta.get("version")
    if not isinstance(src_ver, str) or not re.match(
        r"^\d+\.\d+\.\d+$", src_ver
    ):
        raise ValueError(
            "meta.version must be a semantic version string 'major.minor.patch'"
        )
    rep.info("Spec version: %s", src_ver)
    # Semantic validations across sections
    rep.progress("Validating cross-references (domains/root-signature/heaps)")
    validate_domain_references(doc)
    # Build typed model (normalized) for downstream use
    rep.progress("Building normalized model")
    mdl = model_mod.build_model(doc)
    defaults = doc.get("defaults", {})
    invalid = (
        mdl.defaults.invalid_index
        if mdl
        else defaults.get("invalid_index", 0xFFFFFFFF)
    )
    domains = doc.get("domains", [])
    # Build domain lookup by id for cross-validation with root_signature ranges
    domain_by_id = {d.get("id"): d for d in domains}
    # Validate optional root_signature cross-links
    root_sig = doc.get("root_signature", [])
    # removed: duplicated validations now live in modules

    # removed: handled in domains module

    # removed: handled in root_signature module

    # removed: handled in root_signature module
    ts = time.strftime("%Y-%m-%d %H:%M:%S", time.gmtime())
    rep.progress("Preparing templates (timestamp %s)", ts)

    # Try to compute repo-relative source path using git, fallback to absolute
    def _compute_repo_rel(p: str) -> str:
        try:
            import subprocess

            repo_root = subprocess.check_output(
                ["git", "rev-parse", "--show-toplevel"],
                cwd=os.path.dirname(os.path.abspath(p)),
                text=True,
                stderr=subprocess.DEVNULL,
            ).strip()
            abs_path = os.path.abspath(p)
            # Normalize separators to forward slashes for portability
            rel = os.path.relpath(abs_path, repo_root).replace("\\", "/")
            return rel
        except Exception:
            return os.path.abspath(p)

    src_rel = _compute_repo_rel(input_yaml)
    rep.debug("Source path (repo-relative): %s", src_rel)
    rep.progress("Rendering C++ domain constants")
    domain_consts = render_cpp_domains(domains)
    content_cpp = TEMPLATE_CPP.format(
        src=src_rel,
        src_ver=src_ver,
        schema_ver=schema_version or "",
        tool_ver=TOOL_VERSION,
        ts=ts,
        invalid=invalid,
        domain_consts=domain_consts,
    )
    rep.progress("Rendering HLSL bindless layout")
    domain_defs = render_hlsl_domains(domains)
    content_hlsl = TEMPLATE_HLSL.format(
        src=src_rel,
        src_ver=src_ver,
        schema_ver=schema_version or "",
        tool_ver=TOOL_VERSION,
        ts=ts,
        invalid=invalid,
        domain_defs=domain_defs,
    )

    # RootSignature C++ header content
    rep.progress("Rendering C++ RootSignature header")
    rs_enum, rs_counts, rs_regs = _render_root_sig_cpp(root_sig)
    content_rs = TEMPLATE_RS_CPP.format(
        src=src_rel,
        src_ver=src_ver,
        schema_ver=schema_version or "",
        tool_ver=TOOL_VERSION,
        ts=ts,
        root_param_enums=rs_enum,
        root_constants_counts=rs_counts,
        register_space_consts=rs_regs,
    )

    # Prepare runtime JSON descriptor (machine-friendly)
    # Include heaps/mappings runtime fragment when present and keep the
    # normalized fragment for rendering during dry-run.
    heaps_runtime_fragment = {}
    strategy_json = {}
    if doc.get("heaps"):
        rep.progress("Validating heaps + mappings and building strategy JSON")
        heaps_runtime_fragment = heaps_module.validate_heaps_and_mappings(doc)
        # Build D3D12 strategy description wrapped under top-level 'heaps'
        strategy_json = heaps_module.build_d3d12_strategy_json(
            heaps_runtime_fragment.get("heaps", {})
        )
        # Attach metadata for traceability; keep entries at top-level for compatibility
        strategy_with_meta = {
            "$meta": {
                "source": src_rel,
                "source_version": src_ver,
                "tool_version": TOOL_VERSION,
                "generated": ts,
                "format": "D3D12HeapStrategy/2",
            },
            # strategy_json already has {"heaps": {...}}
        }
        if schema_version:
            strategy_with_meta["$meta"]["schema_version"] = schema_version
        # Merge without flattening 'heaps'
        strategy_with_meta.update(strategy_json)
        strategy_json = strategy_with_meta

    runtime_desc = {
        "source": src_rel,
        "source_version": src_ver,
        "tool_version": TOOL_VERSION,
        "generated": ts,
        **({"schema_version": schema_version} if schema_version else {}),
        "defaults": defaults,
        "domains": domains,
        # include heaps/mappings runtime fragment when present
        **heaps_runtime_fragment,
        "symbols": doc.get("symbols", {}),
        "root_signature": doc.get("root_signature", []),
    }

    # Derive output paths with the new harmonized naming
    # Base is expected to include trailing 'Generated.' prefix; if caller passes
    # a directory or other stem, we will use it as-is and append 'Generated.'
    def _ensure_generated_prefix(p: str) -> str:
        return p if p.endswith("Generated.") else (p + "Generated.")

    if out_base:
        base = _ensure_generated_prefix(out_base)
        out_cpp_path = base + "Constants.h"
        out_hlsl_path = base + "BindlessLayout.hlsl"
        out_json = base + "All.json"
        out_strategy = base + "Heaps.D3D12.json"
        out_rs_path = base + "RootSignature.h"
        out_strategy_hpp = base + "Heaps.D3D12.h"
        out_meta_h = base + "Meta.h"
    else:
        if not (out_cpp and out_hlsl):
            raise ValueError(
                "Either out_base or both out_cpp and out_hlsl must be provided"
            )
        out_cpp_path = out_cpp
        out_hlsl_path = out_hlsl
        # Legacy derivation: rebase to new names using the directory of out_cpp
        base_dir = os.path.dirname(out_cpp_path)
        base = os.path.join(base_dir, "Generated.")
        out_json = base + "All.json"
        out_strategy = base + "Heaps.D3D12.json"
        out_rs_path = base + "RootSignature.h"
        out_strategy_hpp = base + "Heaps.D3D12.h"
        out_meta_h = base + "Meta.h"

    if dry_run:
        rep.info("[DRY RUN] Planned outputs:")
        rep.info("    C++ header: %s", _short(out_cpp_path))
        rep.info("    HLSL layout: %s", _short(out_hlsl_path))
        rep.info("    Runtime JSON: %s", _short(out_json))
        rep.info("    RootSignature C++: %s", _short(out_rs_path))
        rep.info("    Meta C++: %s", _short(out_meta_h))
        if strategy_json:
            rep.info("    D3D12 strategy JSON: %s", _short(out_strategy))
            rep.info("    D3D12 strategy C++: %s", _short(out_strategy_hpp))
        rep.info("Validation successful, templates processed")
        return False
    # Serialize content strings first (no side effects yet)
    js = json.dumps(runtime_desc, indent=2)
    files: dict[str, str] = {
        out_json: js,
        out_cpp_path: content_cpp,
        out_hlsl_path: content_hlsl,
    }
    files[out_rs_path] = content_rs
    # Meta header (always generated)
    content_meta_h = TEMPLATE_META_CPP.format(
        src=src_rel,
        src_ver=src_ver,
        schema_ver=schema_version or "",
        tool_ver=TOOL_VERSION,
        ts=ts,
    )
    files[out_meta_h] = content_meta_h
    if strategy_json:
        sj = json.dumps(strategy_json, indent=2)
        files[out_strategy] = sj
        # Embedded C++ header with constexpr JSON body
        content_strategy_hpp = TEMPLATE_HEAPS_D3D12_CPP.format(
            src=src_rel,
            src_ver=src_ver,
            tool_ver=TOOL_VERSION,
            ts=ts,
            json_body=sj,
        )
        files[out_strategy_hpp] = content_strategy_hpp

    # Transactional write for all outputs
    rep.progress("Writing outputs transactionally")
    changed = transactional_write_files(files)
    if changed:
        rep.info("Outputs updated")
    else:
        rep.info("No changes (up to date)")
    return changed
