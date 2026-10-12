# ===-----------------------------------------------------------------------===#
# Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
# copy at https://opensource.org/licenses/BSD-3-Clause.
# SPDX-License-Identifier: BSD-3-Clause
# ===-----------------------------------------------------------------------===#
# pyright: strict

"""Shared C++/HLSL contract generation: layout rules and emitted code."""

from __future__ import annotations

from pathlib import Path

import pytest
import yaml

from bindless_codegen import contracts
from bindless_codegen.contracts import ContractError, parse_spec

BINDLESS: dict[str, object] = {
    "abi": {
        "domains": [
            {"id": "srv_global", "name": "GlobalSRV"},
            {"id": "materials", "name": "Materials"},
        ]
    }
}


def spec_with(
    structs: list[dict[str, object]],
    constants: list[dict[str, object]] | None = None,
) -> dict[str, object]:
    return {
        "files": [
            {
                "id": "test",
                "cpp": "Oxygen/Test/Generated.Test.h",
                "hlsl": "Test/Generated.Test.hlsli",
                "namespace": "oxygen::test",
                "constants": constants or [],
                "structs": structs,
            }
        ]
    }


def struct(
    name: str,
    layout: str,
    size: int,
    fields: list[dict[str, object]],
    **extra: object,
) -> dict[str, object]:
    return {"name": name, "layout": layout, "size": size, "fields": fields, **extra}


def uints(*names: str) -> list[dict[str, object]]:
    return [{"name": name, "type": "uint"} for name in names]


def test_structured_layout_matches_natural_offsets() -> None:
    spec = parse_spec(
        spec_with(
            [
                struct(
                    "Record",
                    "structured",
                    32,
                    [
                        {"name": "center", "type": "float3"},
                        {"name": "slot", "type": "uint", "slot": "srv_global"},
                        {"name": "extent", "type": "float3"},
                        {"name": "flags", "type": "uint"},
                    ],
                    align=16,
                )
            ]
        ),
        BINDLESS,
    )
    assert spec.structs["Record"].offsets == [0, 12, 16, 28]


def test_implicit_padding_is_rejected() -> None:
    fields: list[dict[str, object]] = [
        {"name": "flags", "type": "uint"},
        {"name": "sequence", "type": "uint64_t"},
    ]
    with pytest.raises(ContractError, match="explicit padding"):
        parse_spec(spec_with([struct("Record", "structured", 16, fields)]), BINDLESS)


def test_declared_size_must_match_the_fields() -> None:
    with pytest.raises(ContractError, match="declares"):
        parse_spec(
            spec_with([struct("Record", "structured", 16, uints("a", "b", "c"))]),
            BINDLESS,
        )


def test_size_must_be_a_multiple_of_the_alignment() -> None:
    with pytest.raises(ContractError, match="multiple of its alignment"):
        parse_spec(
            spec_with(
                [struct("Record", "structured", 12, uints("a", "b", "c"), align=16)]
            ),
            BINDLESS,
        )


def test_constant_layout_rejects_register_straddling() -> None:
    fields: list[dict[str, object]] = [
        {"name": "a", "type": "float2"},
        {"name": "b", "type": "float"},
        {"name": "c", "type": "float2"},
        {"name": "d", "type": "float"},
    ]
    with pytest.raises(ContractError, match="straddles"):
        parse_spec(spec_with([struct("Constants", "constant", 24, fields)]), BINDLESS)


def test_constant_layout_starts_matrices_on_a_register() -> None:
    fields: list[dict[str, object]] = [
        {"name": "a", "type": "float"},
        {"name": "b", "type": "float"},
        {"name": "c", "type": "float"},
        {"name": "d", "type": "float"},
        {"name": "m", "type": "float4x4"},
    ]
    spec = parse_spec(spec_with([struct("Constants", "constant", 80, fields)]), BINDLESS)
    assert spec.structs["Constants"].offsets[-1] == 16

    misplaced: list[dict[str, object]] = [
        {"name": "a", "type": "float"},
        {"name": "m", "type": "float4x4"},
    ]
    with pytest.raises(ContractError, match="16-byte register"):
        parse_spec(spec_with([struct("Constants", "constant", 68, misplaced)]), BINDLESS)


def test_slots_name_a_known_domain() -> None:
    fields: list[dict[str, object]] = [
        {"name": "slot", "type": "uint", "slot": "textures"},
    ]
    with pytest.raises(ContractError, match="unknown domain"):
        parse_spec(spec_with([struct("Record", "structured", 4, fields)]), BINDLESS)


def test_byte_records_reject_matrices() -> None:
    fields: list[dict[str, object]] = [{"name": "m", "type": "float4x4"}]
    with pytest.raises(ContractError, match="byte-address"):
        parse_spec(spec_with([struct("Record", "bytes", 64, fields)]), BINDLESS)


def test_cpp_asserts_every_offset_and_the_size() -> None:
    spec = parse_spec(
        spec_with(
            [
                struct(
                    "Record",
                    "structured",
                    16,
                    [
                        {"name": "slot", "type": "uint", "slot": "srv_global"},
                        {"name": "count", "type": "uint"},
                        {"name": "extent", "type": "float2"},
                    ],
                    align=16,
                )
            ]
        ),
        BINDLESS,
    )
    cpp = contracts.render_cpp(spec, spec.files[0])
    assert "struct alignas(16) Record {" in cpp
    assert "ShaderVisibleIndex slot { kInvalidShaderVisibleIndex };" in cpp
    assert "static_assert(sizeof(Record) == 16U);" in cpp
    assert "static_assert(alignof(Record) == 16U);" in cpp
    assert "static_assert(offsetof(Record, extent) == 8U);" in cpp
    assert "#include <Oxygen/Core/Bindless/Types.h>" in cpp


def test_hlsl_loaders_check_the_declared_domain() -> None:
    spec = parse_spec(
        spec_with(
            [
                struct(
                    "Draw",
                    "structured",
                    8,
                    [
                        {"name": "slot", "type": "uint", "slot": "srv_global"},
                        {"name": "count", "type": "uint"},
                    ],
                    loader={"domain": "materials", "array": True},
                )
            ]
        ),
        BINDLESS,
    )
    hlsl = contracts.render_hlsl(spec, spec.files[0])
    assert "static Draw MakeInvalidDraw()" in hlsl
    assert "value.slot = K_INVALID_BINDLESS_INDEX;" in hlsl
    assert "static bool TryLoadDraw(uint slot, uint index, out Draw value)" in hlsl
    assert "if (!BX_IN_MATERIALS(slot)) {" in hlsl
    assert "if (index >= count) {" in hlsl


def test_byte_records_emit_flattened_offsets_and_accessors() -> None:
    spec = parse_spec(
        spec_with(
            [
                struct("Inner", "bytes", 16, uints("a", "b", "c", "d")),
                struct(
                    "Outer",
                    "bytes",
                    48,
                    [
                        {"name": "head", "type": "Inner"},
                        {"name": "items", "type": "Inner", "count": 2},
                    ],
                    hlsl_prefix="OUTER",
                ),
            ]
        ),
        BINDLESS,
    )
    hlsl = contracts.render_hlsl(spec, spec.files[0])
    assert "static const uint OUTER_HEAD_C_OFFSET = 8u;" in hlsl
    assert "static const uint OUTER_ITEMS_OFFSET = 16u;" in hlsl
    assert "static const uint OUTER_ITEMS_STRIDE = 16u;" in hlsl
    assert "static Outer LoadOuter(ByteAddressBuffer buffer, uint base)" in hlsl
    assert "static void StoreOuter(RWByteAddressBuffer buffer, uint base, Outer value)" in hlsl


def test_constant_buffers_bind_their_register() -> None:
    spec = parse_spec(
        spec_with(
            [
                struct(
                    "ViewConstants",
                    "constant",
                    16,
                    uints("a", "b", "c", "d"),
                    register="b1",
                    align=16,
                )
            ]
        ),
        BINDLESS,
    )
    hlsl = contracts.render_hlsl(spec, spec.files[0])
    assert "cbuffer ViewConstants : register(b1, space0)" in hlsl
    assert "struct ViewConstants" not in hlsl


def test_constants_render_on_both_sides() -> None:
    spec = parse_spec(
        spec_with(
            [],
            [{"cpp": "kFaceCount", "hlsl": "FACE_COUNT", "type": "uint", "value": 6}],
        ),
        BINDLESS,
    )
    assert "inline constexpr std::uint32_t kFaceCount = 6U;" in contracts.render_cpp(
        spec, spec.files[0]
    )
    assert "static const uint FACE_COUNT = 6u;" in contracts.render_hlsl(
        spec, spec.files[0]
    )


def test_check_mode_reports_stale_outputs(tmp_path: Path) -> None:
    spec_path = tmp_path / "contracts.yaml"
    bindless_path = tmp_path / "bindless.yaml"
    spec_path.write_text(
        yaml.safe_dump(spec_with([struct("Record", "structured", 4, uints("a"))]))
    )
    bindless_path.write_text(yaml.safe_dump(BINDLESS))
    cpp_root = tmp_path / "src"
    hlsl_root = tmp_path / "shaders"
    stale = contracts.generate(
        spec_path, bindless_path, cpp_root, hlsl_root, check=True
    )
    assert len(stale) == 2
    written = contracts.generate(spec_path, bindless_path, cpp_root, hlsl_root)
    assert written == stale
    assert not contracts.generate(
        spec_path, bindless_path, cpp_root, hlsl_root, check=True
    )


def test_engine_contracts_are_current() -> None:
    engine = Path(__file__).resolve().parents[6]
    stale = contracts.generate(
        engine / "src/Oxygen/Vortex/Meta/ShaderContracts.yaml",
        engine / "src/Oxygen/Core/Meta/Bindless.yaml",
        engine / "src",
        engine / "src/Oxygen/Graphics/Direct3D12/Shaders",
        check=True,
    )
    assert not stale
