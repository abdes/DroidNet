"""Dataclass models for structured PAK specification (refactored)."""

from __future__ import annotations
from dataclasses import dataclass, field
from typing import List, Dict, Optional, Any


@dataclass(slots=True)
class BufferResource:
    name: str
    data: bytes = b""
    usage: int = 0
    stride: int = 0
    format: int = 0
    bind_flags: int = 0


@dataclass(slots=True)
class TextureResource:
    name: str
    data: bytes = b""
    texture_type: int = 0
    compression_type: int = 0
    width: int = 0
    height: int = 0
    depth: int = 1
    array_layers: int = 1
    mip_levels: int = 1
    format: int = 0
    alignment: int = 256


@dataclass(slots=True)
class AudioResource:
    name: str
    data: bytes = b""


@dataclass(slots=True)
class ShaderReference:
    source_path: str
    entry_point: str
    defines: str = ""
    shader_hash: int = 0


@dataclass(slots=True)
class MaterialAsset:
    name: str
    asset_key: bytes
    material_domain: int = 0
    flags: int = 0
    shader_stages: int = 0
    base_color: List[float] = field(
        default_factory=lambda: [1.0, 1.0, 1.0, 1.0]
    )
    normal_scale: float = 1.0
    metalness: float = 0.0
    roughness: float = 1.0
    ambient_occlusion: float = 1.0
    texture_refs: Dict[str, Optional[str]] = field(default_factory=dict)
    shader_references: List[ShaderReference] = field(default_factory=list)
    version: int = 1
    streaming_priority: int = 0
    content_hash: int = 0
    variant_flags: int = 0
    alignment: int = 1
    uv_scale: List[float] = field(default_factory=lambda: [1.0, 1.0])
    uv_offset: List[float] = field(default_factory=lambda: [0.0, 0.0])
    uv_rotation_radians: float = 0.0
    uv_set: int = 0
    grid_spacing: List[float] = field(default_factory=lambda: [1.0, 1.0])
    grid_major_every: int = 10
    grid_line_thickness: float = 1.0
    grid_major_thickness: float = 2.0
    grid_axis_thickness: float = 2.0
    grid_fade_start: float = 0.0
    grid_fade_end: float = 0.0
    grid_minor_color: List[float] = field(
        default_factory=lambda: [0.35, 0.35, 0.35, 1.0]
    )
    grid_major_color: List[float] = field(
        default_factory=lambda: [0.55, 0.55, 0.55, 1.0]
    )
    grid_axis_color_x: List[float] = field(
        default_factory=lambda: [0.9, 0.2, 0.2, 1.0]
    )
    grid_axis_color_y: List[float] = field(
        default_factory=lambda: [0.2, 0.6, 0.9, 1.0]
    )
    grid_origin_color: List[float] = field(
        default_factory=lambda: [1.0, 1.0, 1.0, 1.0]
    )


@dataclass(slots=True)
class MeshView:
    first_index: int = 0
    index_count: int = 0
    first_vertex: int = 0
    vertex_count: int = 0


@dataclass(slots=True)
class Submesh:
    name: str
    material: str
    mesh_views: List[MeshView] = field(default_factory=list)
    bounding_box_min: List[float] = field(
        default_factory=lambda: [0.0, 0.0, 0.0]
    )
    bounding_box_max: List[float] = field(
        default_factory=lambda: [0.0, 0.0, 0.0]
    )


@dataclass(slots=True)
class GeometryLod:
    name: str
    mesh_type: int = 0
    vertex_buffer: Optional[str] = None
    index_buffer: Optional[str] = None
    joint_index_buffer: Optional[str] = None
    joint_weight_buffer: Optional[str] = None
    inverse_bind_buffer: Optional[str] = None
    joint_remap_buffer: Optional[str] = None
    skeleton_asset_key: Optional[str] = None
    joint_count: int = 0
    influences_per_vertex: int = 0
    skinned_flags: int = 0
    submeshes: List[Submesh] = field(default_factory=list)
    bounding_box_min: List[float] = field(
        default_factory=lambda: [0.0, 0.0, 0.0]
    )
    bounding_box_max: List[float] = field(
        default_factory=lambda: [0.0, 0.0, 0.0]
    )


@dataclass(slots=True)
class GeometryAsset:
    name: str
    asset_key: bytes
    lods: List[GeometryLod] = field(default_factory=list)
    bounding_box_min: List[float] = field(
        default_factory=lambda: [0.0, 0.0, 0.0]
    )
    bounding_box_max: List[float] = field(
        default_factory=lambda: [0.0, 0.0, 0.0]
    )
    alignment: int = 1
    version: int = 1
    streaming_priority: int = 0
    content_hash: int = 0
    variant_flags: int = 0


@dataclass(slots=True)
class PakSpec:
    version: int = 4
    content_version: int = 0
    buffers: List[BufferResource] = field(default_factory=list)
    textures: List[TextureResource] = field(default_factory=list)
    audios: List[AudioResource] = field(default_factory=list)
    # Single authoritative list of asset dict objects (each containing a 'type').
    assets: List[Any] = field(default_factory=list)

    def iter_assets(self) -> List[Any]:  # simple accessor
        return list(self.assets)


__all__ = [
    "BufferResource",
    "TextureResource",
    "AudioResource",
    "ShaderReference",
    "MaterialAsset",
    "MeshView",
    "Submesh",
    "GeometryLod",
    "GeometryAsset",
    "PakSpec",
]
