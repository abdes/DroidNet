# ===-----------------------------------------------------------------------===#
# Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
# copy at https://opensource.org/licenses/BSD-3-Clause.
# SPDX-License-Identifier: BSD-3-Clause
# ===-----------------------------------------------------------------------===#
# pyright: strict

"""Static samplers occupy fixed, named slots of a sampler domain."""

from __future__ import annotations

from typing import Any

import pytest

from bindless_codegen import domains

DOMAINS: list[dict[str, Any]] = [
    {
        "id": "samplers",
        "name": "Samplers",
        "shader_index_base": 0,
        "capacity": 4,
        "shader_access_class": "sampler",
    },
    {
        "id": "textures",
        "name": "Textures",
        "shader_index_base": 8,
        "capacity": 4,
        "shader_access_class": "texture_srv",
    },
]


def sampler(sampler_id: str, slot: int, domain: str = "samplers") -> dict[str, Any]:
    return {"id": sampler_id, "domain": domain, "slot": slot}


def test_slots_render_as_named_indices() -> None:
    samplers = [sampler("linear_wrap", 0), sampler("point_clamp", 2)]
    domains.validate_static_samplers(samplers, DOMAINS)
    cpp = domains.render_cpp_static_samplers(samplers, DOMAINS)
    hlsl = domains.render_hlsl_static_samplers(samplers, DOMAINS)
    assert "inline constexpr uint32_t kPointClampSamplerIndex = 2U;" in cpp
    assert "static const uint K_SAMPLER_LINEAR_WRAP = 0;" in hlsl


@pytest.mark.parametrize(
    ("samplers", "message"),
    [
        ([sampler("a", 4)], "outside domain"),
        ([sampler("a", 0, "textures")], "sampler domain"),
        ([sampler("a", 0), sampler("b", 0)], "not unique"),
        ([sampler("a", 0), sampler("a", 1)], "not unique"),
    ],
)
def test_invalid_slots_are_rejected(
    samplers: list[dict[str, Any]], message: str
) -> None:
    with pytest.raises(ValueError, match=message):
        domains.validate_static_samplers(samplers, DOMAINS)
