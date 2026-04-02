# ===-----------------------------------------------------------------------===#
# Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
# copy at https://opensource.org/licenses/BSD-3-Clause.
# SPDX-License-Identifier: BSD-3-Clause
# ===-----------------------------------------------------------------------===#

from __future__ import annotations

from copy import deepcopy
from typing import Any, Dict


def create_minimal_valid_document() -> Dict[str, Any]:
    return {
        "meta": {"version": "2.0.0"},
        "defaults": {"invalid_index": 4294967295},
        "abi": {
            "index_spaces": [
                {"id": "srv_uav_cbv"},
                {"id": "sampler"},
            ],
            "domains": [
                {
                    "id": "test_domain",
                    "name": "TestDomain",
                    "index_space": "srv_uav_cbv",
                    "shader_index_base": 1,
                    "capacity": 100,
                    "shader_access_class": "buffer_srv",
                    "view_types": ["StructuredBuffer_SRV"],
                }
            ]
        },
        "backends": {
            "d3d12": {
                "strategy": {
                    "heaps": [
                        {
                            "id": "CBV_SRV_UAV:gpu",
                            "type": "CBV_SRV_UAV",
                            "shader_visible": True,
                            "capacity": 512,
                            "base_index": 1000,
                            "allow_growth": False,
                        }
                    ],
                    "tables": [
                        {
                            "id": "Table0",
                            "descriptor_kind": "SRV",
                            "heap": "CBV_SRV_UAV:gpu",
                            "shader_register": "t0",
                            "register_space": "space0",
                            "descriptor_count": 256,
                        }
                    ],
                    "domain_realizations": [
                        {
                            "domain": "test_domain",
                            "table": "Table0",
                            "heap_local_base": 0,
                        }
                    ],
                },
                "root_signature": [
                    {
                        "type": "descriptor_table",
                        "id": "Table0",
                        "table": "Table0",
                        "index": 0,
                        "visibility": "ALL",
                    }
                ],
            },
            "vulkan": {
                "strategy": {
                    "descriptor_sets": [
                        {"id": "bindless_main", "set": 0}
                    ],
                    "bindings": [
                        {
                            "id": "buffers_binding",
                            "set": "bindless_main",
                            "binding": 0,
                            "descriptor_type": "STORAGE_BUFFER",
                            "descriptor_count": 256,
                        }
                    ],
                    "domain_realizations": [
                        {
                            "domain": "test_domain",
                            "binding": "buffers_binding",
                            "array_element_base": 1,
                        }
                    ],
                },
                "pipeline_layout": [
                    {
                        "type": "descriptor_set",
                        "id": "BindlessMain",
                        "set_ref": "bindless_main",
                    }
                ],
            },
        },
    }


def create_full_example_document() -> Dict[str, Any]:
    doc = create_minimal_valid_document()
    doc["meta"]["description"] = "Test bindless ABI plus backend realizations"
    doc["abi"]["domains"] = [
        {
            "id": "global_buffers",
            "name": "GlobalBuffers",
            "index_space": "srv_uav_cbv",
            "shader_index_base": 1,
            "capacity": 2048,
            "shader_access_class": "buffer_srv",
            "view_types": ["StructuredBuffer_SRV", "RawBuffer_SRV"],
            "comment": "Structured/raw SRV buffers",
        },
        {
            "id": "textures",
            "name": "Textures",
            "index_space": "srv_uav_cbv",
            "shader_index_base": 4096,
            "capacity": 8192,
            "shader_access_class": "texture_srv",
            "view_types": ["Texture_SRV"],
            "comment": "Texture array",
        },
        {
            "id": "samplers",
            "name": "Samplers",
            "index_space": "sampler",
            "shader_index_base": 0,
            "capacity": 256,
            "shader_access_class": "sampler",
            "view_types": ["Sampler"],
            "comment": "Sampler array",
        },
    ]
    doc["backends"]["d3d12"] = {
        "strategy": {
            "heaps": [
                {
                    "id": "CBV_SRV_UAV:gpu",
                    "type": "CBV_SRV_UAV",
                    "shader_visible": True,
                    "capacity": 20000,
                    "base_index": 1000000,
                    "allow_growth": False,
                },
                {
                    "id": "SAMPLER:gpu",
                    "type": "SAMPLER",
                    "shader_visible": True,
                    "capacity": 512,
                    "base_index": 2000000,
                    "allow_growth": False,
                },
            ],
            "tables": [
                {
                    "id": "SrvTable",
                    "descriptor_kind": "SRV",
                    "heap": "CBV_SRV_UAV:gpu",
                    "shader_register": "t0",
                    "register_space": "space0",
                    "unbounded": True,
                },
                {
                    "id": "SamplerTable",
                    "descriptor_kind": "SAMPLER",
                    "heap": "SAMPLER:gpu",
                    "shader_register": "s0",
                    "register_space": "space0",
                    "descriptor_count": 256,
                },
            ],
            "domain_realizations": [
                {
                    "domain": "global_buffers",
                    "table": "SrvTable",
                    "heap_local_base": 1,
                },
                {
                    "domain": "textures",
                    "table": "SrvTable",
                    "heap_local_base": 4096,
                },
                {
                    "domain": "samplers",
                    "table": "SamplerTable",
                    "heap_local_base": 0,
                },
            ],
        },
        "root_signature": [
            {
                "type": "descriptor_table",
                "id": "BindlessSrvTable",
                "table": "SrvTable",
                "index": 0,
                "visibility": ["ALL"],
            },
            {
                "type": "descriptor_table",
                "id": "BindlessSamplerTable",
                "table": "SamplerTable",
                "index": 1,
                "visibility": ["ALL"],
            },
            {
                "type": "cbv",
                "id": "ViewConstants",
                "index": 2,
                "visibility": ["ALL"],
                "shader_register": "b1",
                "register_space": "space0",
            },
        ],
    }
    doc["backends"]["vulkan"] = {
        "strategy": {
            "descriptor_sets": [
                {"id": "bindless_main", "set": 0}
            ],
            "bindings": [
                {
                    "id": "buffers_binding",
                    "set": "bindless_main",
                    "binding": 0,
                    "descriptor_type": "STORAGE_BUFFER",
                    "descriptor_count": 8192,
                },
                {
                    "id": "textures_binding",
                    "set": "bindless_main",
                    "binding": 1,
                    "descriptor_type": "SAMPLED_IMAGE",
                    "descriptor_count": 16384,
                },
                {
                    "id": "samplers_binding",
                    "set": "bindless_main",
                    "binding": 2,
                    "descriptor_type": "SAMPLER",
                    "descriptor_count": 256,
                },
            ],
            "domain_realizations": [
                {
                    "domain": "global_buffers",
                    "binding": "buffers_binding",
                    "array_element_base": 1,
                },
                {
                    "domain": "textures",
                    "binding": "textures_binding",
                    "array_element_base": 4096,
                },
                {
                    "domain": "samplers",
                    "binding": "samplers_binding",
                    "array_element_base": 0,
                },
            ],
        },
        "pipeline_layout": [
            {
                "type": "descriptor_set",
                "id": "BindlessMain",
                "set_ref": "bindless_main",
            },
            {
                "type": "push_constants",
                "id": "RootConstants",
                "size_bytes": 8,
                "stages": ["ALL"],
            },
        ],
    }
    return doc


def copy_document(doc: Dict[str, Any]) -> Dict[str, Any]:
    return deepcopy(doc)
