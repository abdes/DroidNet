//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <string_view>

namespace oxygen::content::import {

inline constexpr std::string_view kImportManifestSchema = R"({
"$schema": "http://json-schema.org/draft-07/schema#",
"title": "Oxygen Import Manifest",
"type": "object",
"additionalProperties": false,
"properties": {
    "version": {
        "type": "integer",
        "const": 1
    },
    "thread_pool_size": {
        "type": "integer",
        "minimum": 1
    },
    "max_in_flight_jobs": {
        "type": "integer",
        "minimum": 1
    },
    "concurrency": {
        "type": "object",
        "properties": {
            "texture": {
                "$ref": "#/definitions/concurrency_item"
            },
            "buffer": {
                "$ref": "#/definitions/concurrency_item"
            },
            "material": {
                "$ref": "#/definitions/concurrency_item"
            },
            "mesh_build": {
                "$ref": "#/definitions/concurrency_item"
            },
            "geometry": {
                "$ref": "#/definitions/concurrency_item"
            },
            "scene": {
                "$ref": "#/definitions/concurrency_item"
            }
        },
        "additionalProperties": false
    },
    "defaults": {
        "$ref": "#/definitions/defaults_settings"
    },
    "jobs": {
        "type": "array",
        "items": {
            "$ref": "#/definitions/job_item"
        }
    }
},
"required": [
    "jobs"
],
"definitions": {
    "concurrency_item": {
        "type": "object",
        "properties": {
            "workers": {
                "type": "integer",
                "minimum": 1
            },
            "queue_capacity": {
                "type": "integer",
                "minimum": 1
            }
        },
        "required": [
            "workers",
            "queue_capacity"
        ],
        "additionalProperties": false
    },
    "job_item": {
        "type": "object",
        "allOf": [
            {
                "$ref": "#/definitions/job_settings"
            },
            {
                "required": [
                    "source",
                    "type"
                ]
            }
        ]
    },
    "defaults_settings": {
        "type": "object",
        "properties": {
            "texture": {
                "$ref": "#/definitions/texture_settings"
            },
            "scene": {
                "$ref": "#/definitions/scene_settings"
            }
        },
        "additionalProperties": false
    },
    "texture_settings": {
        "type": "object",
        "properties": {
            "sources": {
                "type": "array",
                "items": {
                    "type": "object",
                    "properties": {
                        "file": {
                            "type": "string"
                        },
                        "layer": {
                            "type": "integer",
                            "minimum": 0
                        },
                        "mip": {
                            "type": "integer",
                            "minimum": 0
                        },
                        "slice": {
                            "type": "integer",
                            "minimum": 0
                        }
                    },
                    "required": [
                        "file"
                    ],
                    "additionalProperties": false
                }
            },
            "output": {
                "type": "string"
            },
            "name": {
                "type": "string"
            },
            "verbose": {
                "type": "boolean"
            },
            "intent": {
                "$ref": "#/definitions/texture_intent"
            },
            "color_space": {
                "$ref": "#/definitions/color_space"
            },
            "output_format": {
                "$ref": "#/definitions/texture_format"
            },
            "data_format": {
                "$ref": "#/definitions/texture_format"
            },
            "preset": {
                "$ref": "#/definitions/texture_preset"
            },
            "mip_policy": {
                "$ref": "#/definitions/mip_policy"
            },
            "mip_filter": {
                "$ref": "#/definitions/mip_filter"
            },
            "mip_filter_space": {
                "$ref": "#/definitions/color_space"
            },
            "bc7_quality": {
                "$ref": "#/definitions/bc7_quality"
            },
            "packing_policy": {
                "$ref": "#/definitions/packing_policy"
            },
            "cube_layout": {
                "$ref": "#/definitions/cube_layout"
            },
            "hdr_handling": {
                "$ref": "#/definitions/hdr_handling"
            },
            "exposure_ev": {
                "type": "number"
            },
            "max_mips": {
                "type": "integer",
                "minimum": 1
            },
            "cube_face_size": {
                "type": "integer",
                "minimum": 1
            },
            "flip_y": {
                "type": "boolean"
            },
            "flip_normal_green": {
                "type": "boolean"
            },
            "renormalize": {
                "type": "boolean"
            },
            "bake_hdr": {
                "type": "boolean"
            },
            "force_rgba": {
                "type": "boolean"
            },
            "cubemap": {
                "type": "boolean"
            },
            "equirect_to_cube": {
                "type": "boolean"
            },
            "content_hashing": {
                "type": "boolean"
            }
        },
        "additionalProperties": false
    },
    "scene_settings": {
        "type": "object",
        "properties": {
            "output": {
                "type": "string"
            },
            "name": {
                "type": "string"
            },
            "verbose": {
                "type": "boolean"
            },
            "content_hashing": {
                "type": "boolean"
            },
            "content_flags": {
                "type": "object",
                "properties": {
                    "textures": {
                        "type": "boolean"
                    },
                    "materials": {
                        "type": "boolean"
                    },
                    "geometry": {
                        "type": "boolean"
                    },
                    "scene": {
                        "type": "boolean"
                    }
                },
                "additionalProperties": false
            },
            "unit_policy": {
                "enum": [
                    "normalize",
                    "preserve",
                    "custom"
                ]
            },
            "unit_scale": {
                "type": "number",
                "minimum": 0.0001
            },
            "bake_transforms": {
                "type": "boolean"
            },
            "normals_policy": {
                "enum": [
                    "none",
                    "preserve",
                    "generate",
                    "recalculate"
                ]
            },
            "tangents_policy": {
                "enum": [
                    "none",
                    "preserve",
                    "generate",
                    "recalculate"
                ]
            },
            "node_pruning": {
                "enum": [
                    "keep",
                    "drop-empty"
                ]
            },
            "texture_overrides": {
                "type": "object",
                "additionalProperties": {
                    "$ref": "#/definitions/texture_settings"
                }
            }
        },
        "additionalProperties": false
    },
    "job_settings": {
        "type": "object",
        "properties": {
            "type": {
                "enum": [
                    "texture",
                    "fbx",
                    "gltf"
                ]
            },
            "source": {
                "type": "string"
            },
            "sources": {
                "type": "array",
                "items": {
                    "type": "object",
                    "properties": {
                        "file": {
                            "type": "string"
                        },
                        "layer": {
                            "type": "integer",
                            "minimum": 0
                        },
                        "mip": {
                            "type": "integer",
                            "minimum": 0
                        },
                        "slice": {
                            "type": "integer",
                            "minimum": 0
                        }
                    },
                    "required": [
                        "file"
                    ],
                    "additionalProperties": false
                }
            },
            "output": {
                "type": "string"
            },
            "name": {
                "type": "string"
            },
            "verbose": {
                "type": "boolean"
            },
            "intent": {
                "$ref": "#/definitions/texture_intent"
            },
            "color_space": {
                "$ref": "#/definitions/color_space"
            },
            "output_format": {
                "$ref": "#/definitions/texture_format"
            },
            "data_format": {
                "$ref": "#/definitions/texture_format"
            },
            "preset": {
                "$ref": "#/definitions/texture_preset"
            },
            "mip_policy": {
                "$ref": "#/definitions/mip_policy"
            },
            "mip_filter": {
                "$ref": "#/definitions/mip_filter"
            },
            "mip_filter_space": {
                "$ref": "#/definitions/color_space"
            },
            "bc7_quality": {
                "$ref": "#/definitions/bc7_quality"
            },
            "packing_policy": {
                "$ref": "#/definitions/packing_policy"
            },
            "cube_layout": {
                "$ref": "#/definitions/cube_layout"
            },
            "hdr_handling": {
                "$ref": "#/definitions/hdr_handling"
            },
            "exposure_ev": {
                "type": "number"
            },
            "max_mips": {
                "type": "integer",
                "minimum": 1
            },
            "cube_face_size": {
                "type": "integer",
                "minimum": 1
            },
            "flip_y": {
                "type": "boolean"
            },
            "flip_normal_green": {
                "type": "boolean"
            },
            "renormalize": {
                "type": "boolean"
            },
            "bake_hdr": {
                "type": "boolean"
            },
            "force_rgba": {
                "type": "boolean"
            },
            "cubemap": {
                "type": "boolean"
            },
            "equirect_to_cube": {
                "type": "boolean"
            },
            "content_hashing": {
                "type": "boolean"
            },
            "content_flags": {
                "type": "object",
                "properties": {
                    "textures": {
                        "type": "boolean"
                    },
                    "materials": {
                        "type": "boolean"
                    },
                    "geometry": {
                        "type": "boolean"
                    },
                    "scene": {
                        "type": "boolean"
                    }
                },
                "additionalProperties": false
            },
            "unit_policy": {
                "enum": [
                    "normalize",
                    "preserve",
                    "custom"
                ]
            },
            "unit_scale": {
                "type": "number",
                "minimum": 0.0001
            },
            "bake_transforms": {
                "type": "boolean"
            },
            "normals_policy": {
                "enum": [
                    "none",
                    "preserve",
                    "generate",
                    "recalculate"
                ]
            },
            "tangents_policy": {
                "enum": [
                    "none",
                    "preserve",
                    "generate",
                    "recalculate"
                ]
            },
            "node_pruning": {
                "enum": [
                    "keep",
                    "drop-empty"
                ]
            },
            "texture_overrides": {
                "type": "object",
                "additionalProperties": {
                    "$ref": "#/definitions/texture_settings"
                }
            }
        },
        "additionalProperties": false
    },
    "texture_intent": {
        "enum": [
            "albedo",
            "normal",
            "roughness",
            "metallic",
            "ao",
            "emissive",
            "opacity",
            "orm",
            "hdr_env",
            "hdr-env",
            "hdr_probe",
            "hdr-probe",
            "data",
            "height"
        ]
    },
    "color_space": {
        "enum": [
            "srgb",
            "linear"
        ]
    },
    "texture_format": {
        "enum": [
            "rgba8",
            "rgba8_srgb",
            "rgba8-srgb",
            "bc7",
            "bc7_srgb",
            "bc7-srgb",
            "rgba16f",
            "rgba32f"
        ]
    },
    "texture_preset": {
        "enum": [
            "albedo",
            "albedo-srgb",
            "albedo-linear",
            "normal",
            "normal-bc7",
            "roughness",
            "metallic",
            "ao",
            "orm",
            "orm-bc7",
            "emissive",
            "ui",
            "hdr-env",
            "hdr-env-16f",
            "hdr-env-32f",
            "hdr-probe",
            "data",
            "height"
        ]
    },
    "mip_policy": {
        "enum": [
            "none",
            "full",
            "max"
        ]
    },
    "mip_filter": {
        "enum": [
            "box",
            "kaiser",
            "lanczos"
        ]
    },
    "bc7_quality": {
        "enum": [
            "none",
            "fast",
            "default",
            "high"
        ]
    },
    "packing_policy": {
        "enum": [
            "d3d12",
            "tight"
        ]
    },
    "cube_layout": {
        "enum": [
            "auto",
            "hstrip",
            "vstrip",
            "hcross",
            "vcross"
        ]
    },
    "hdr_handling": {
        "enum": [
            "error",
            "tonemap",
            "auto",
            "keep",
            "float"
        ]
    }
}
})";

} // namespace oxygen::content::import
