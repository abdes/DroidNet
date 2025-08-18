# ===-----------------------------------------------------------------------===#
# Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
# copy at https://opensource.org/licenses/BSD-3-Clause.
# SPDX-License-Identifier: BSD-3-Clause
# ===-----------------------------------------------------------------------===#

import json
import subprocess
import os
import sys
from pathlib import Path

import pytest

from bindless_codegen.generator import generate


THIS_DIR = Path(__file__).resolve().parent


def run_gen_inprocess(tmp_path, strategy):
    out_base = tmp_path / "Generated"
    out_base.parent.mkdir(parents=True, exist_ok=True)
    out_base_str = str(out_base) + "."
    # Call generate() directly in-process which is faster and avoids
    # environment-dependent subprocess machinery.
    # Spec.yaml lives under src/Oxygen/Core/Bindless/Spec.yaml
    src = str((THIS_DIR.parent.parent.parent / "Bindless" / "Spec.yaml"))
    # generate returns True when files changed; we want the JSON path
    generate(
        src,
        None,
        None,
        dry_run=False,
        schema_path=None,
        out_base=out_base_str,
        reporter=None,
        ts_strategy=strategy,
    )
    js_path = out_base_str + "All.json"
    with open(js_path, "r", encoding="utf-8") as f:
        return json.load(f)


def test_ts_preserve(tmp_path):
    data1 = run_gen_inprocess(tmp_path / "p1", "preserve")
    g1 = data1.get("generated")
    assert isinstance(g1, str)
    data2 = run_gen_inprocess(tmp_path / "p2", "preserve")
    g2 = data2.get("generated")
    assert isinstance(g2, str)


def test_ts_omit(tmp_path):
    data = run_gen_inprocess(tmp_path / "o", "omit")
    g = data.get("generated", "")
    assert g == "" or g is None


def test_ts_git_sha(tmp_path):
    data = run_gen_inprocess(tmp_path / "g", "git-sha")
    g = data.get("generated")
    assert isinstance(g, str)
    assert len(g) > 0
