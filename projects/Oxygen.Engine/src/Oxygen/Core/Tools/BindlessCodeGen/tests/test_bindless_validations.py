# ===-----------------------------------------------------------------------===#
# Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
# copy at https://opensource.org/licenses/BSD-3-Clause.
# SPDX-License-Identifier: BSD-3-Clause
# ===-----------------------------------------------------------------------===#

import pytest
import yaml

from bindless_codegen import generator

from spec_fixtures import copy_document, create_full_example_document


def write_yaml(path, content):
    with open(path, "w", encoding="utf-8") as stream:
        yaml.safe_dump(content, stream, sort_keys=False)
    return str(path)


def test_abi_domain_overlap_detection(tmp_path):
    doc = create_full_example_document()
    doc["abi"]["domains"][1]["shader_index_base"] = 1024

    path = write_yaml(tmp_path / "overlap.yaml", doc)
    with pytest.raises(ValueError, match="ABI shader-visible range .* overlap"):
        generator.generate(path, "out.cpp", "out.hlsl", dry_run=True)


def test_incompatible_view_types_vs_access_class(tmp_path):
    doc = create_full_example_document()
    doc["abi"]["domains"][0]["view_types"] = ["Texture_SRV"]

    path = write_yaml(tmp_path / "view_types.yaml", doc)
    with pytest.raises(ValueError, match="not compatible with shader_access_class"):
        generator.generate(path, "out.cpp", "out.hlsl", dry_run=True)


def test_d3d12_heap_ranges_overlap_detection(tmp_path):
    doc = create_full_example_document()
    doc["backends"]["d3d12"]["strategy"]["heaps"][1]["base_index"] = 1000100

    path = write_yaml(tmp_path / "d3d12_heaps.yaml", doc)
    with pytest.raises(ValueError, match="heap address ranges overlap"):
        generator.generate(path, "out.cpp", "out.hlsl", dry_run=True)


def test_d3d12_root_signature_unknown_table_rejected(tmp_path):
    doc = create_full_example_document()
    doc["backends"]["d3d12"]["root_signature"][0]["table"] = "MissingTable"

    path = write_yaml(tmp_path / "root_sig.yaml", doc)
    with pytest.raises(ValueError, match="references unknown table"):
        generator.generate(path, "out.cpp", "out.hlsl", dry_run=True)


def test_vulkan_binding_overlap_detection(tmp_path):
    doc = create_full_example_document()
    doc["abi"]["domains"].append(
        {
            "id": "materials",
            "name": "Materials",
            "index_space": "srv_uav_cbv",
            "shader_index_base": 2049,
            "capacity": 512,
            "shader_access_class": "buffer_srv",
            "view_types": ["StructuredBuffer_SRV"],
        }
    )
    doc["backends"]["d3d12"]["strategy"]["domain_realizations"].append(
        {
            "domain": "materials",
            "table": "SrvTable",
            "heap_local_base": 2049,
        }
    )
    doc["backends"]["vulkan"]["strategy"]["domain_realizations"].append(
        {
            "domain": "materials",
            "binding": "buffers_binding",
            "array_element_base": 100,
        }
    )

    path = write_yaml(tmp_path / "vk_overlap.yaml", doc)
    with pytest.raises(ValueError, match="binding-local realization overlap"):
        generator.generate(path, "out.cpp", "out.hlsl", dry_run=True)


def test_missing_backend_domain_realization_rejected(tmp_path):
    doc = create_full_example_document()
    doc["backends"]["vulkan"]["strategy"]["domain_realizations"] = (
        doc["backends"]["vulkan"]["strategy"]["domain_realizations"][:-1]
    )

    path = write_yaml(tmp_path / "missing_vk_domain.yaml", doc)
    with pytest.raises(ValueError, match="missing domain realizations"):
        generator.generate(path, "out.cpp", "out.hlsl", dry_run=True)
