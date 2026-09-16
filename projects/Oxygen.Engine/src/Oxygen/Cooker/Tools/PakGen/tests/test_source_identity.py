"""Caller-owned canonical UUIDv7 source identities at every producer boundary."""

import json
from uuid import UUID

import pytest

from pakgen.api import BuildOptions, build_pak, plan_dry_run, validate_spec
from pakgen.packing.errors import PakError
from pakgen.packing.packers import pack_header
from pakgen.packing.source_identity import source_identity_bytes
from pakgen.spec.validator import run_validation_pipeline


IDENTITY = "0194a6d0-1a20-7b42-8ab3-fc384e6d5179"
MISSING_IDENTITY = object()
INVALID_IDENTITIES = [
    None,
    7,
    True,
    {},
    "",
    "not-a-uuid",
    "00000000-0000-0000-0000-000000000000",
    "0194a6d0-1a20-4b42-8ab3-fc384e6d5179",
    "0194a6d0-1a20-5b42-8ab3-fc384e6d5179",
    "0194a6d0-1a20-7b42-cab3-fc384e6d5179",
    IDENTITY.upper(),
    IDENTITY.replace("-", ""),
    "{" + IDENTITY + "}",
]


def _spec():
    return {"version": 7, "source_identity": IDENTITY, "assets": []}


@pytest.mark.parametrize("identity", INVALID_IDENTITIES)
def test_schema_and_header_boundary_reject_noncanonical_identity(identity):
    spec = _spec()
    spec["source_identity"] = identity
    assert "E_SOURCE_IDENTITY" in {
        error.code for error in run_validation_pipeline(spec)
    }
    with pytest.raises(PakError, match="E_SOURCE_IDENTITY"):
        source_identity_bytes(identity)


@pytest.mark.parametrize("deterministic", [False, True])
@pytest.mark.parametrize("identity", [MISSING_IDENTITY] + INVALID_IDENTITIES)
def test_invalid_identity_is_rejected_before_any_output_write(
    tmp_path, deterministic, identity
):
    spec = _spec()
    if identity is MISSING_IDENTITY:
        spec.pop("source_identity")
    else:
        spec["source_identity"] = identity
    source = tmp_path / "source.json"
    source.write_text(json.dumps(spec), encoding="utf-8")
    output = tmp_path / "existing.pak"
    output.write_bytes(b"preserve existing output")
    with pytest.raises(ValueError, match="E_SOURCE_IDENTITY"):
        build_pak(
            BuildOptions(
                input_spec=source,
                output_path=output,
                deterministic=deterministic,
                force=True,
            )
        )
    assert output.read_bytes() == b"preserve existing output"
    with pytest.raises(ValueError, match="E_SOURCE_IDENTITY"):
        plan_dry_run(source, deterministic=deterministic)
    with pytest.raises(ValueError, match="E_SOURCE_IDENTITY"):
        validate_spec(source)


@pytest.mark.parametrize(
    "identity",
    [
        bytes(16),
        bytes(15),
        UUID("0194a6d0-1a20-4b42-8ab3-fc384e6d5179").bytes,
        UUID("0194a6d0-1a20-5b42-8ab3-fc384e6d5179").bytes,
        UUID("0194a6d0-1a20-7b42-cab3-fc384e6d5179").bytes,
    ],
)
def test_direct_header_packer_rejects_invalid_identity_bytes(identity):
    with pytest.raises(PakError, match="E_SOURCE_IDENTITY"):
        pack_header(7, 0, identity)


@pytest.mark.parametrize("deterministic", [False, True])
def test_authored_identity_survives_content_changes_and_output_paths(
    tmp_path, deterministic
):
    source = tmp_path / "source.json"
    spec = _spec()
    outputs = []
    for content_version in (1, 2):
        spec["content_version"] = content_version
        source.write_text(json.dumps(spec), encoding="utf-8")
        validate_spec(source)
        output = tmp_path / f"content-{content_version}.pak"
        build_pak(
            BuildOptions(
                input_spec=source,
                output_path=output,
                deterministic=deterministic,
            )
        )
        outputs.append(output.read_bytes())
    assert outputs[0] != outputs[1]
    assert outputs[0][12:28] == outputs[1][12:28] == UUID(IDENTITY).bytes
