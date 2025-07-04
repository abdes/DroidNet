# ===----------------------------------------------------------------------===//
# Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
# copy at https://opensource.org/licenses/BSD-3-Clause.
# SPDX-License-Identifier: BSD-3-Clause
# ===----------------------------------------------------------------------===//
"""
Oxygen PAK File Generator
========================

This script generates Oxygen Engine PAK files for testing, based on a
declarative YAML or JSON specification.

- Maps the C++ PAK format (see PakFormat.h) to Python classes.
- Supports YAML/JSON input for easy, human-friendly test asset description.
- Handles assets by type with dedicated processors for each type:
  * Material assets: Create MaterialAssetDesc structures with texture references
  * Geometry assets: Complex multi-LOD meshes with submeshes and material references
  * Other assets: Shader, texture, audio, etc. using generic data handling
- Can be used as a CLI tool or imported as a module.

Usage:
  python generate_pak.py <spec.yaml> <output.pak>

See --help for more details.
"""

import argparse
import json
import os
import shutil
import struct
import sys
import tempfile
import time
import zlib
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, BinaryIO, Dict, List, Optional, Tuple, Union

import yaml

# === Logging Utilities ===---------------------------------------------------//


class Logger:
    """Structured logger with verbosity levels and progress tracking."""

    def __init__(self, verbose_mode: bool = False):
        self.verbose_mode = verbose_mode
        self.indent_level = 0
        self.start_time = time.time()

    def _print(
        self,
        message: str,
        level: str = "INFO",
        indent_override: Optional[int] = None,
    ):
        """Print message with proper indentation and formatting."""
        indent = "  " * (
            indent_override
            if indent_override is not None
            else self.indent_level
        )
        timestamp = (
            f"[{time.time() - self.start_time:06.2f}s]"
            if self.verbose_mode
            else ""
        )
        print(f"{timestamp}{indent}[{level}] {message}")

    def info(self, message: str):
        """Log info message."""
        self._print(message, "PAK")

    def verbose(self, message: str):
        """Log verbose message (only shown with --verbose)."""
        if self.verbose_mode:
            self._print(message, "DBG")

    def success(self, message: str):
        """Log success message."""
        self._print(message, "✓")

    def warning(self, message: str):
        """Log warning message."""
        self._print(message, "⚠")

    def error(self, message: str):
        """Log error message."""
        self._print(message, "✗", indent_override=0)

    def section(self, title: str):
        """Start a new section with increased indentation."""
        self._print(f"{title}...", "PAK")
        self.indent_level += 1
        return SectionContext(self)

    def step(self, description: str, count: Optional[int] = None):
        """Log a processing step with optional count."""
        count_str = f" ({count} items)" if count is not None else ""
        self._print(f"{description}{count_str}", "→")


class SectionContext:
    """Context manager for logging sections."""

    def __init__(self, logger: Logger):
        self.logger = logger

    def __enter__(self):
        return self.logger

    def __exit__(self, exc_type, exc_val, exc_tb):
        self.logger.indent_level -= 1


# Global logger instance
_logger: Optional[Logger] = None


def get_logger() -> Logger:
    """Get the global logger instance."""
    global _logger
    if _logger is None:
        _logger = Logger()
    return _logger


def set_logger(logger: Logger):
    """Set the global logger instance."""
    global _logger
    _logger = logger


# === Constants ===------------------------------------------------------------//

# PAK File Format Constants
MAGIC = b"OXPAK\x00\x00\x00"
FOOTER_MAGIC = b"OXPAKEND"
HEADER_SIZE = 64
FOOTER_SIZE = 256
DIRECTORY_ENTRY_SIZE = 64

# Asset and Resource Constants
ASSET_NAME_MAX_LENGTH = 63
ASSET_KEY_SIZE = 16
MATERIAL_DESC_SIZE = 256
GEOMETRY_DESC_SIZE = 256
MESH_DESC_SIZE = 104
SUBMESH_DESC_SIZE = 108
MESH_VIEW_DESC_SIZE = 16

# Alignment Constants
DATA_ALIGNMENT = 256
TABLE_ALIGNMENT = 16

# Resource Entry Sizes
RESOURCE_ENTRY_SIZES = {
    "texture": 40,
    "buffer": 32,
    "audio": 32,
}

# Asset Type Mapping (matches C++ AssetType enum in AssetType.h)
ASSET_TYPE_MAP = {
    "unknown": 0,  # kUnknown
    "material": 1,  # kMaterial
    "geometry": 2,  # kGeometry
    "scene": 3,  # kScene
}

# === Data Structures ===-----------------------------------------------------//


@dataclass
class ResourceRegion:
    """Represents a resource region in the PAK file."""

    offset: int = 0
    size: int = 0

    def pack(self) -> bytes:
        """Pack the resource region into binary format."""
        return struct.pack("<QQ", self.offset, self.size)


@dataclass
class ResourceTable:
    """Represents a resource table in the PAK file."""

    offset: int = 0
    count: int = 0
    entry_size: int = 0

    def pack(self) -> bytes:
        """Pack the resource table into binary format."""
        return struct.pack("<QII", self.offset, self.count, self.entry_size)


@dataclass
class PakHeader:
    """PAK file header structure."""

    version: int
    content_version: int
    magic: bytes = field(default=MAGIC)
    reserved: bytes = field(default_factory=lambda: b"\x00" * 52)

    def pack(self) -> bytes:
        """Pack the header into binary format."""
        return struct.pack(
            "<8sHH52s",
            self.magic,
            self.version,
            self.content_version,
            self.reserved,
        )


@dataclass
class PakFooter:
    """PAK file footer structure."""

    directory_offset: int
    directory_size: int
    asset_count: int
    texture_region: ResourceRegion = field(default_factory=ResourceRegion)
    buffer_region: ResourceRegion = field(default_factory=ResourceRegion)
    audio_region: ResourceRegion = field(default_factory=ResourceRegion)
    texture_table: ResourceTable = field(default_factory=ResourceTable)
    buffer_table: ResourceTable = field(default_factory=ResourceTable)
    audio_table: ResourceTable = field(default_factory=ResourceTable)
    pak_crc32: int = 0
    reserved: bytes = field(default_factory=lambda: b"\x00" * 124)
    footer_magic: bytes = field(default=FOOTER_MAGIC)

    def pack(self) -> bytes:
        """Pack the footer into binary format."""
        return (
            struct.pack(
                "<QQQ",
                self.directory_offset,
                self.directory_size,
                self.asset_count,
            )
            + self.texture_region.pack()
            + self.buffer_region.pack()
            + self.audio_region.pack()
            + self.texture_table.pack()
            + self.buffer_table.pack()
            + self.audio_table.pack()
            + struct.pack("<I", self.pak_crc32)
            + self.reserved
            + self.footer_magic
        )


@dataclass
class AssetDirectoryEntry:
    """Asset directory entry structure."""

    asset_key: bytes
    asset_type: int
    entry_offset: int
    desc_offset: int
    desc_size: int
    reserved: bytes = field(default_factory=lambda: b"\x00" * 27)

    def __post_init__(self):
        """Validate asset key size."""
        if len(self.asset_key) != ASSET_KEY_SIZE:
            raise ValueError(
                f"Asset key must be {ASSET_KEY_SIZE} bytes, "
                f"got {len(self.asset_key)}"
            )

    def pack(self) -> bytes:
        """Pack the directory entry into binary format."""
        # AssetKey is exactly 16 bytes, no padding needed
        return (
            self.asset_key
            + struct.pack(
                "<BQQI",
                self.asset_type,
                self.entry_offset,
                self.desc_offset,
                self.desc_size,
            )
            + self.reserved
        )


# === Utility Functions ===---------------------------------------------------//


def calculate_pak_crc32(file_path: Union[str, Path]) -> int:
    """Calculate CRC32 hash of PAK file content (excluding the CRC32 field itself).

    Args:
        file_path: Path to the PAK file

    Returns:
        32-bit CRC32 hash as unsigned integer
    """
    import zlib

    file_path = Path(file_path)
    hash_value = 0

    with open(file_path, "rb") as f:
        f.seek(0, 2)  # Seek to end
        file_size = f.tell()

        # Read content in two parts: before CRC32 field and after CRC32 field
        # CRC32 field is at offset: file_size - 12 (4 bytes CRC32 + 8 bytes magic)
        crc32_field_offset = file_size - 12

        f.seek(0)  # Back to start
        # Read everything before the CRC32 field
        content_before_crc32 = f.read(crc32_field_offset)

        # Skip the CRC32 field (4 bytes)
        f.seek(crc32_field_offset + 4)
        # Read everything after the CRC32 field (magic)
        content_after_crc32 = f.read()

        # Combine and hash
        full_content = content_before_crc32 + content_after_crc32
        hash_value = (
            zlib.crc32(full_content) & 0xFFFFFFFF
        )  # Ensure unsigned 32-bit

    return hash_value


def safe_file_path(base_dir: Path, file_path: str) -> Path:
    """Safely resolve file path to prevent directory traversal attacks.

    Args:
        base_dir: Base directory for relative paths
        file_path: File path to resolve

    Returns:
        Resolved path within base_dir

    Raises:
        ValueError: If path attempts to escape base_dir
    """
    base_dir = base_dir.resolve()
    resolved_path = (base_dir / file_path).resolve()

    # Ensure the resolved path is within base_dir
    try:
        resolved_path.relative_to(base_dir)
    except ValueError:
        raise ValueError(
            f"File path '{file_path}' attempts to escape base directory"
        )

    return resolved_path


def validate_name_length(
    name: str, max_length: int = ASSET_NAME_MAX_LENGTH
) -> str:
    """Validate and truncate name to maximum length."""
    if len(name.encode("utf-8")) > max_length:
        logger = get_logger()
        logger.warning(f"Name '{name}' truncated to {max_length} bytes")
    return name


def pack_name_string(name: str, size: int) -> bytes:
    """Pack a name string to a fixed size with null termination."""
    name = validate_name_length(name, size - 1)
    name_bytes = name.encode("utf-8")[: size - 1]
    return name_bytes + b"\x00" * (size - len(name_bytes))


def parse_guid_string(guid_str: str) -> bytes:
    """Parse GUID string into 16-byte binary format.

    Accepts formats:
    - '01234567-89ab-cdef-0123-456789abcdef'
    - '0123456789abcdef0123456789abcdef'
    - '{01234567-89ab-cdef-0123-456789abcdef}'
    """
    hex_str = (
        guid_str.replace("-", "")
        .replace("{", "")
        .replace("}", "")
        .replace(" ", "")
    )
    if len(hex_str) != 32:
        raise ValueError(f"GUID string must have 32 hex digits: {guid_str}")
    try:
        return bytes.fromhex(hex_str)
    except ValueError as e:
        raise ValueError(f"Invalid GUID format: {guid_str}") from e


def get_asset_key(asset: Dict[str, Any]) -> bytes:
    """Extract asset key from asset specification."""
    if "asset_key" in asset:
        return parse_guid_string(asset["asset_key"])
    return b"\x00" * ASSET_KEY_SIZE


def align_file(file_obj: BinaryIO, alignment: int) -> int:
    """Align file position to the specified boundary."""
    pos = file_obj.tell()
    pad = (alignment - (pos % alignment)) % alignment
    if pad:
        file_obj.write(b"\x00" * pad)
    return pad


def load_spec(path: Union[str, Path]) -> Dict[str, Any]:
    """Load specification from YAML or JSON file."""
    path = Path(path)
    if not path.exists():
        raise FileNotFoundError(f"Specification file not found: {path}")

    with open(path, "r", encoding="utf-8") as f:
        if path.suffix.lower() in {".yaml", ".yml"}:
            try:
                return yaml.safe_load(f)
            except yaml.YAMLError as e:
                raise ValueError(f"Invalid YAML format: {e}") from e
        elif path.suffix.lower() == ".json":
            try:
                return json.load(f)
            except json.JSONDecodeError as e:
                raise ValueError(f"Invalid JSON format: {e}") from e
        else:
            raise ValueError("Spec file must be .yaml, .yml, or .json")


def resolve_asset_type(type_str: str) -> int:
    """Resolve asset type string to integer constant."""
    asset_type = type_str.lower()
    if asset_type not in ASSET_TYPE_MAP:
        raise ValueError(
            f"Unknown asset type: {type_str}. "
            f"Valid types: {list(ASSET_TYPE_MAP.keys())}"
        )
    return ASSET_TYPE_MAP[asset_type]


def validate_specification(spec: Dict[str, Any]) -> None:
    """Validate specification structure and required fields.

    Args:
        spec: Specification dictionary to validate

    Raises:
        ValueError: If specification is invalid
    """
    logger = get_logger()

    with logger.section("Validating specification structure"):
        # Check top-level structure
        if not isinstance(spec, dict):
            raise ValueError("Specification must be a dictionary")

        # Check version fields
        version = spec.get("version")
        if version is not None and not isinstance(version, int):
            raise ValueError("'version' must be an integer")

        content_version = spec.get("content_version")
        if content_version is not None and not isinstance(content_version, int):
            raise ValueError("'content_version' must be an integer")

        # Validate resource sections
        resource_types = ["textures", "buffers", "audios"]
        for resource_type in resource_types:
            if resource_type in spec:
                resources = spec[resource_type]
                if not isinstance(resources, list):
                    raise ValueError(f"'{resource_type}' must be a list")

                for i, resource in enumerate(resources):
                    if not isinstance(resource, dict):
                        raise ValueError(
                            f"{resource_type}[{i}] must be a dictionary"
                        )
                    if "name" not in resource:
                        raise ValueError(
                            f"{resource_type}[{i}] missing required 'name' field"
                        )

        # Validate assets section
        if "assets" in spec:
            assets = spec["assets"]
            if not isinstance(assets, list):
                raise ValueError("'assets' must be a list")

            for i, asset in enumerate(assets):
                if not isinstance(asset, dict):
                    raise ValueError(f"assets[{i}] must be a dictionary")
                if "name" not in asset:
                    raise ValueError(
                        f"assets[{i}] missing required 'name' field"
                    )
                if "type" not in asset:
                    raise ValueError(
                        f"assets[{i}] missing required 'type' field"
                    )

                # Validate asset type
                asset_type = asset["type"]
                if asset_type.lower() not in ASSET_TYPE_MAP:
                    raise ValueError(
                        f"assets[{i}] has invalid type '{asset_type}'. "
                        f"Valid types: {list(ASSET_TYPE_MAP.keys())}"
                    )

        logger.step("Specification structure is valid")


# === Data Reading Functions ===----------------------------------------------//


def read_data_from_spec(spec_entry: Dict[str, Any], base_dir: Path) -> bytes:
    """Unified function to read data from specification entry.

    Supports three data sources:
    - 'data_hex': Hexadecimal string
    - 'file': File path relative to base_dir
    - 'data': Direct string or bytes data
    """
    if "data_hex" in spec_entry:
        try:
            return bytes.fromhex(spec_entry["data_hex"])
        except ValueError as e:
            raise ValueError(f"Invalid hex data: {e}") from e

    if "file" in spec_entry:
        file_path = safe_file_path(base_dir, spec_entry["file"])
        if not file_path.exists():
            raise FileNotFoundError(f"Data file not found: {file_path}")
        try:
            return file_path.read_bytes()
        except OSError as e:
            raise OSError(f"Failed to read file {file_path}: {e}") from e

    if "data" in spec_entry:
        data = spec_entry["data"]
        if isinstance(data, str):
            return data.encode("utf-8")
        elif isinstance(data, bytes):
            return data
        else:
            raise TypeError("Data must be string or bytes")

    return b""


# === Asset Processing Functions ===------------------------------------------//


def create_asset_header(asset: Dict[str, Any]) -> bytes:
    """Create common AssetHeader (95 bytes) for any asset type.

    Args:
        asset: Asset specification dictionary

    Returns:
        Binary AssetHeader structure
    """
    name = asset["name"]
    asset_type = resolve_asset_type(asset["type"])
    version = asset.get("version", 1)
    streaming_priority = asset.get("streaming_priority", 0)
    content_hash = asset.get("content_hash", 0)
    variant_flags = asset.get("variant_flags", 0)

    name_bytes = pack_name_string(name, 64)
    header = (
        struct.pack("<B", asset_type)
        + name_bytes
        + struct.pack("<B", version)
        + struct.pack("<B", streaming_priority)
        + struct.pack("<Q", content_hash)
        + struct.pack("<I", variant_flags)
        + b"\x00" * 16  # reserved
    )

    if len(header) != 95:  # AssetHeader size
        raise RuntimeError(
            f"Asset header size mismatch: expected 95, got {len(header)}"
        )

    return header


def create_shader_references(shader_refs: List[Dict[str, Any]]) -> bytes:
    """Create shader reference descriptors (216 bytes each).

    Args:
        shader_refs: List of shader reference specifications

    Returns:
        Binary shader reference data
    """
    if not shader_refs:
        return b""

    shader_data = b""
    for shader_ref in shader_refs:
        shader_id = shader_ref.get("shader_unique_id", "")
        shader_hash = shader_ref.get("shader_hash", 0)

        # Pack shader unique ID (192 bytes)
        shader_id_bytes = pack_name_string(shader_id, 192)

        # Create ShaderReferenceDesc (216 bytes)
        shader_desc = (
            shader_id_bytes
            + struct.pack("<Q", shader_hash)
            + b"\x00" * 16  # reserved
        )

        if len(shader_desc) != 216:  # ShaderReferenceDesc size
            raise RuntimeError(
                f"Shader reference size mismatch: expected 216, got {len(shader_desc)}"
            )

        shader_data += shader_desc

    return shader_data


def handle_material_asset(
    asset: Dict[str, Any],
    resource_index_map: Dict[str, Dict[str, int]],
    base_dir: Path,
) -> Dict[str, Any]:
    """Handle material asset processing.

    Args:
        asset: Asset specification dictionary
        resource_index_map: Resource index mapping
        base_dir: Base directory for relative paths

    Returns:
        Processed material asset data
    """
    logger = get_logger()

    name = asset["name"]
    asset_key = get_asset_key(asset)
    asset_type = resolve_asset_type(asset["type"])
    alignment = asset.get("alignment", 1)

    # Create material descriptor
    data = create_material_asset_descriptor(asset, resource_index_map)

    logger.verbose(f"    Material asset: {len(data)} bytes")

    return {
        "key": asset_key,
        "type": asset_type,
        "name": name,
        "data": data,
        "alignment": alignment,
    }


def handle_geometry_asset(
    asset: Dict[str, Any],
) -> Tuple[Dict[str, Any], bytes, int, int]:
    """Handle geometry asset processing.

    Args:
        asset: Asset specification dictionary

    Returns:
        Tuple of (asset, asset_key, asset_type, alignment)
    """
    logger = get_logger()

    asset_key = get_asset_key(asset)
    asset_type = resolve_asset_type(asset["type"])
    alignment = asset.get("alignment", 1)
    lod_count = len(asset.get("lods", []))

    logger.verbose(f"    Geometry asset with {lod_count} LODs")

    return (asset, asset_key, asset_type, alignment)


# === Asset Collection Functions ===---------------------------------------//


def create_material_asset_descriptor(
    asset: Dict[str, Any], resource_index_map: Dict[str, Dict[str, int]]
) -> bytes:
    """Create MaterialAssetDesc binary structure from asset specification.

    Structure:
    - AssetHeader (95 bytes)
    - MaterialAssetDesc specific fields (161 bytes)
    - Followed by shader references (216 bytes each)
    """
    # Extract material properties with defaults
    material_domain = asset.get("material_domain", 0)
    flags = asset.get("flags", 0)
    shader_stages = asset.get("shader_stages", 0)
    base_color = asset.get("base_color", [1.0, 1.0, 1.0, 1.0])
    normal_scale = asset.get("normal_scale", 1.0)
    metalness = asset.get("metalness", 0.0)
    roughness = asset.get("roughness", 1.0)
    ambient_occlusion = asset.get("ambient_occlusion", 1.0)

    # Resolve texture references
    texture_refs = asset.get("texture_refs", {})
    texture_map = resource_index_map.get("texture", {})

    def get_texture_index(field_name: str) -> int:
        """Get texture index for a material texture field."""
        ref = texture_refs.get(field_name)
        return texture_map.get(ref, 0) if ref else 0

    base_color_texture = get_texture_index("base_color_texture")
    normal_texture = get_texture_index("normal_texture")
    metallic_texture = get_texture_index("metallic_texture")
    roughness_texture = get_texture_index("roughness_texture")
    ambient_occlusion_texture = get_texture_index("ambient_occlusion_texture")
    reserved_textures = [0] * 8

    # Create common AssetHeader (95 bytes)
    header = create_asset_header(asset)

    # Create MaterialAssetDesc specific fields (161 bytes)
    material_desc = (
        header
        + struct.pack("<B", material_domain)
        + struct.pack("<I", flags)
        + struct.pack("<I", shader_stages)
        + struct.pack("<4f", *base_color)
        + struct.pack("<f", normal_scale)
        + struct.pack("<f", metalness)
        + struct.pack("<f", roughness)
        + struct.pack("<f", ambient_occlusion)
        + struct.pack("<I", base_color_texture)
        + struct.pack("<I", normal_texture)
        + struct.pack("<I", metallic_texture)
        + struct.pack("<I", roughness_texture)
        + struct.pack("<I", ambient_occlusion_texture)
        + b"".join(struct.pack("<I", t) for t in reserved_textures)
        + b"\x00" * 68  # reserved
    )

    if len(material_desc) != MATERIAL_DESC_SIZE:
        raise RuntimeError(
            f"Material descriptor size mismatch: expected "
            f"{MATERIAL_DESC_SIZE}, got {len(material_desc)}"
        )

    # Add shader references if present
    shader_refs = asset.get("shader_references", [])
    if shader_refs:
        shader_data = create_shader_references(shader_refs)
        material_desc += shader_data

    return material_desc


# === Resource Descriptor Creation Functions ===------------------------------//


def create_buffer_resource_descriptor(
    resource_spec: Dict[str, Any], data_offset: int, data_size: int
) -> bytes:
    """Create BufferResourceDesc binary structure from resource specification.

    Structure (32 bytes):
    - data_offset: 8 bytes (OffsetT)
    - size_bytes: 4 bytes (DataBlobSizeT)
    - usage_flags: 4 bytes (uint32_t)
    - element_stride: 4 bytes (uint32_t)
    - element_format: 1 byte (uint8_t)
    - reserved: 11 bytes (uint8_t[11])
    """
    # Map YAML field names to descriptor fields
    usage_flags = resource_spec.get("usage", 0)  # YAML uses "usage"
    element_stride = resource_spec.get("stride", 0)  # YAML uses "stride"
    element_format = resource_spec.get("format", 0)  # YAML uses "format"
    bind_flags = resource_spec.get(
        "bind_flags", 0
    )  # Additional YAML field for completeness

    desc = (
        struct.pack("<Q", data_offset)  # data_offset (8 bytes)
        + struct.pack("<I", data_size)  # size_bytes (4 bytes)
        + struct.pack("<I", usage_flags)  # usage_flags (4 bytes)
        + struct.pack("<I", element_stride)  # element_stride (4 bytes)
        + struct.pack("<B", element_format)  # element_format (1 byte)
        + b"\x00" * 11  # reserved (11 bytes)
    )

    if len(desc) != 32:  # BufferResourceDesc size
        raise RuntimeError(
            f"Buffer descriptor size mismatch: expected 32, got {len(desc)}"
        )

    return desc


def create_texture_resource_descriptor(
    resource_spec: Dict[str, Any], data_offset: int, data_size: int
) -> bytes:
    """Create TextureResourceDesc binary structure from resource specification.

    Structure (40 bytes):
    - data_offset: 8 bytes (OffsetT)
    - data_size: 4 bytes (DataBlobSizeT)
    - texture_type: 1 byte (uint8_t)
    - compression_type: 1 byte (uint8_t)
    - width: 4 bytes (uint32_t)
    - height: 4 bytes (uint32_t)
    - depth: 2 bytes (uint16_t)
    - array_layers: 2 bytes (uint16_t)
    - mip_levels: 2 bytes (uint16_t)
    - format: 1 byte (uint8_t)
    - alignment: 2 bytes (uint16_t)
    - is_cubemap: 1 byte (uint8_t)
    - reserved: 8 bytes (uint8_t[8])
    """
    texture_type = resource_spec.get("texture_type", 0)
    compression_type = resource_spec.get("compression_type", 0)
    width = resource_spec.get("width", 0)
    height = resource_spec.get("height", 0)
    depth = resource_spec.get("depth", 1)
    array_layers = resource_spec.get("array_layers", 1)
    mip_levels = resource_spec.get("mip_levels", 1)
    format_val = resource_spec.get("format", 0)
    alignment = resource_spec.get("alignment", 256)
    is_cubemap = resource_spec.get("is_cubemap", 0)

    desc = (
        struct.pack("<Q", data_offset)  # data_offset (8 bytes)
        + struct.pack("<I", data_size)  # data_size (4 bytes)
        + struct.pack("<B", texture_type)  # texture_type (1 byte)
        + struct.pack("<B", compression_type)  # compression_type (1 byte)
        + struct.pack("<I", width)  # width (4 bytes)
        + struct.pack("<I", height)  # height (4 bytes)
        + struct.pack("<H", depth)  # depth (2 bytes)
        + struct.pack("<H", array_layers)  # array_layers (2 bytes)
        + struct.pack("<H", mip_levels)  # mip_levels (2 bytes)
        + struct.pack("<B", format_val)  # format (1 byte)
        + struct.pack("<H", alignment)  # alignment (2 bytes)
        + struct.pack("<B", is_cubemap)  # is_cubemap (1 byte)
        + b"\x00" * 8  # reserved (8 bytes)
    )

    if len(desc) != 40:  # TextureResourceDesc size
        raise RuntimeError(
            f"Texture descriptor size mismatch: expected 40, got {len(desc)}"
        )

    return desc


def create_audio_resource_descriptor(
    resource_spec: Dict[str, Any], data_offset: int, data_size: int
) -> bytes:
    """Create AudioResourceDesc binary structure from resource specification.

    Structure (32 bytes) - placeholder for audio resources:
    - data_offset: 8 bytes (OffsetT)
    - data_size: 4 bytes (DataBlobSizeT)
    - reserved: 20 bytes (uint8_t[20])
    """
    desc = (
        struct.pack("<Q", data_offset)  # data_offset (8 bytes)
        + struct.pack("<I", data_size)  # data_size (4 bytes)
        + b"\x00" * 20  # reserved (20 bytes)
    )

    if len(desc) != 32:  # AudioResourceDesc size
        raise RuntimeError(
            f"Audio descriptor size mismatch: expected 32, got {len(desc)}"
        )

    return desc


# === Resource Collection Functions ===---------------------------------------//


@dataclass
class ResourceCollectionResult:
    """Result of resource collection operation."""

    data_blobs: Dict[str, List[bytes]]
    desc_fields: Dict[str, List[Dict[str, Any]]]
    index_map: Dict[str, Dict[str, int]]


def collect_resources(
    spec: Dict[str, Any], base_dir: Path, resource_types: List[str]
) -> ResourceCollectionResult:
    """Collect resource data and descriptors from specification."""
    logger = get_logger()
    data_blobs = {rtype: [] for rtype in resource_types}
    desc_fields = {rtype: [] for rtype in resource_types}
    index_map = {rtype: {} for rtype in resource_types}

    with logger.section("Collecting resources from specification"):
        total_resources = sum(
            len(spec.get(rtype + "s", [])) for rtype in resource_types
        )
        logger.step(
            f"Processing {total_resources} resources across {len(resource_types)} types"
        )

        for resource_type in resource_types:
            plural_key = resource_type + "s"
            resource_list = spec.get(plural_key, [])

            if resource_list:
                logger.step(
                    f"Processing {resource_type} resources", len(resource_list)
                )

                for idx, resource_spec in enumerate(resource_list, 1):
                    if "name" not in resource_spec:
                        raise ValueError(
                            f"Resource {idx} missing required 'name' field"
                        )

                    name = resource_spec["name"]
                    if name in index_map[resource_type]:
                        raise ValueError(f"Duplicate resource name: {name}")

                    try:
                        data = read_data_from_spec(resource_spec, base_dir)
                        data_blobs[resource_type].append(data)
                        index_map[resource_type][name] = idx
                        desc_fields[resource_type].append(resource_spec)
                        logger.verbose(
                            f"  {resource_type.capitalize()} '{name}': {len(data)} bytes"
                        )
                    except Exception as e:
                        raise RuntimeError(
                            f"Failed to process {resource_type} "
                            f"resource '{name}': {e}"
                        ) from e
            else:
                logger.verbose(f"No {resource_type} resources found")

    return ResourceCollectionResult(data_blobs, desc_fields, index_map)


@dataclass
class AssetCollectionResult:
    """Result of asset collection operation."""

    material_assets: List[Dict[str, Any]]
    geometry_assets: List[Tuple[Dict[str, Any], bytes, int, int]]


def collect_assets(
    spec: Dict[str, Any],
    resource_index_map: Dict[str, Dict[str, int]],
    base_dir: Path,
) -> AssetCollectionResult:
    """Collect assets from specification, organized by type.

    Assets are processed using type-specific handlers:
    - Material assets: Use specialized MaterialAssetDesc structure with shader references
    - Geometry assets: Complex hierarchical mesh data with LODs and submeshes

    When adding new asset types, create new handle_<type>_asset functions.
    """
    logger = get_logger()
    assets = spec.get("assets", [])
    material_assets = []
    geometry_assets = []

    with logger.section("Collecting assets from specification"):
        if not assets:
            logger.warning("No assets found in specification")
            return AssetCollectionResult(material_assets, geometry_assets)

        logger.step(f"Processing assets", len(assets))

        for i, asset in enumerate(assets, 1):
            if "name" not in asset:
                raise ValueError("Asset missing required 'name' field")
            if "type" not in asset:
                raise ValueError(
                    f"Asset '{asset['name']}' missing required 'type' field"
                )

            name = asset["name"]
            asset_type_str = asset["type"].lower()

            logger.verbose(f"  Asset {i}: '{name}' (type: {asset_type_str})")

            try:
                if asset_type_str == "material":
                    processed_asset = handle_material_asset(
                        asset, resource_index_map, base_dir
                    )
                    material_assets.append(processed_asset)
                elif asset_type_str == "geometry":
                    processed_asset = handle_geometry_asset(asset)
                    geometry_assets.append(processed_asset)
                else:
                    raise ValueError(
                        f"Unknown asset type: {asset_type_str}. "
                        f"Supported types: material, geometry"
                    )
            except Exception as e:
                raise RuntimeError(
                    f"Failed to process asset '{name}': {e}"
                ) from e

        # Log collection summary
        total_assets = len(material_assets) + len(geometry_assets)
        logger.step(
            f"Collection complete: {len(material_assets)} material assets, {len(geometry_assets)} geometry assets"
        )

    return AssetCollectionResult(material_assets, geometry_assets)


# === PAK Writing Functions ===-----------------------------------------------//


def write_resource_data_blobs(
    file_obj: BinaryIO,
    resource_types: List[str],
    resource_data_blobs: Dict[str, List[bytes]],
) -> Tuple[Dict[str, Tuple[int, int]], Dict[str, List[int]]]:
    """Write resource data blobs to file and return region/offset information."""
    logger = get_logger()
    resource_region_offsets = {}
    resource_data_offsets = {rtype: [] for rtype in resource_types}

    with logger.section("Writing resource data regions"):
        total_blobs = sum(len(blobs) for blobs in resource_data_blobs.values())
        logger.step(
            f"Processing {total_blobs} resource blobs across {len(resource_types)} types"
        )

        for resource_type in resource_types:
            blobs = resource_data_blobs[resource_type]
            if blobs:
                logger.step(f"Writing {resource_type} data region", len(blobs))
                logger.verbose(f"Aligning to {DATA_ALIGNMENT}-byte boundary")

                align_file(file_obj, DATA_ALIGNMENT)
                region_offset = file_obj.tell()

                for i, data in enumerate(blobs):
                    align_file(file_obj, DATA_ALIGNMENT)
                    data_offset = file_obj.tell()
                    file_obj.write(data)
                    resource_data_offsets[resource_type].append(data_offset)
                    logger.verbose(
                        f"  Blob {i+1}: {len(data)} bytes at offset 0x{data_offset:08x}"
                    )

                region_size = file_obj.tell() - region_offset
                resource_region_offsets[resource_type] = (
                    region_offset,
                    region_size,
                )
                logger.verbose(
                    f"  Region: {region_size} bytes at offset 0x{region_offset:08x}"
                )
            else:
                resource_region_offsets[resource_type] = (0, 0)
                logger.verbose(f"No {resource_type} resources to write")

    return resource_region_offsets, resource_data_offsets


def write_resource_tables(
    file_obj: BinaryIO,
    resource_types: List[str],
    resource_data_blobs: Dict[str, List[bytes]],
    resource_desc_fields: Dict[str, List[Dict[str, Any]]],
    resource_data_offsets: Dict[str, List[int]],
) -> Dict[str, Tuple[int, int, int]]:
    """Write resource tables to file and return table information."""
    logger = get_logger()
    resource_table_offsets = {}

    with logger.section("Writing resource tables"):
        total_entries = sum(
            len(fields) for fields in resource_desc_fields.values()
        )
        logger.step(f"Processing {total_entries} resource table entries")

        for resource_type in resource_types:
            desc_fields = resource_desc_fields[resource_type]
            blobs = resource_data_blobs[resource_type]

            if desc_fields:
                logger.step(
                    f"Writing {resource_type} resource table", len(desc_fields)
                )
                logger.verbose(f"Aligning to {TABLE_ALIGNMENT}-byte boundary")

                align_file(file_obj, TABLE_ALIGNMENT)
                table_offset = file_obj.tell()
                entry_size = RESOURCE_ENTRY_SIZES[resource_type]

                for i, resource_spec in enumerate(desc_fields):
                    # Get data offset and size
                    data_offset = 0
                    data_size = 0
                    if blobs and i < len(resource_data_offsets[resource_type]):
                        data_offset = resource_data_offsets[resource_type][i]
                        data_blob = blobs[i]
                        data_size = len(data_blob)
                        logger.verbose(
                            f"    Data: {data_size} bytes at offset 0x{data_offset:08x}"
                        )

                    # Create resource descriptor based on resource type
                    if resource_type == "buffer":
                        desc = create_buffer_resource_descriptor(
                            resource_spec, data_offset, data_size
                        )
                    elif resource_type == "texture":
                        desc = create_texture_resource_descriptor(
                            resource_spec, data_offset, data_size
                        )
                    elif resource_type == "audio":
                        desc = create_audio_resource_descriptor(
                            resource_spec, data_offset, data_size
                        )
                    else:
                        raise ValueError(
                            f"Unknown resource type: {resource_type}"
                        )

                    logger.verbose(
                        f"  Entry {i+1}: Created {resource_type} descriptor ({len(desc)} bytes)"
                    )

                    file_obj.write(desc)

                resource_table_offsets[resource_type] = (
                    table_offset,
                    len(desc_fields),
                    entry_size,
                )
                logger.verbose(
                    f"  Table: {len(desc_fields)} entries × {entry_size} bytes at offset 0x{table_offset:08x}"
                )
            else:
                resource_table_offsets[resource_type] = (0, 0, 0)
                logger.verbose(f"No {resource_type} resource table to write")

    return resource_table_offsets


def create_geometry_asset_descriptor(
    asset: Dict[str, Any], resource_index_map: Dict[str, Dict[str, int]]
) -> bytes:
    """Create geometry asset descriptor and write LOD data."""
    lods = asset.get("lods", [])
    bounding_box_min = asset.get("bounding_box_min", [0.0, 0.0, 0.0])
    bounding_box_max = asset.get("bounding_box_max", [0.0, 0.0, 0.0])

    # Create common AssetHeader (95 bytes)
    header = create_asset_header(asset)

    # Create geometry descriptor
    geometry_desc = (
        header
        + struct.pack("<I", len(lods))
        + struct.pack("<3f", *bounding_box_min)
        + struct.pack("<3f", *bounding_box_max)
        + b"\x00" * 133  # reserved
    )

    if len(geometry_desc) != GEOMETRY_DESC_SIZE:
        raise RuntimeError(
            f"Geometry descriptor size mismatch: expected "
            f"{GEOMETRY_DESC_SIZE}, got {len(geometry_desc)}"
        )

    return geometry_desc


def create_mesh_descriptor(
    lod: Dict[str, Any], resource_index_map: Dict[str, Dict[str, int]]
) -> bytes:
    """Create mesh descriptor from LOD specification."""
    mesh_name = pack_name_string(lod["name"], 64)
    vertex_buffer_idx = resource_index_map["buffer"].get(
        lod["vertex_buffer"], 0
    )
    index_buffer_idx = resource_index_map["buffer"].get(lod["index_buffer"], 0)
    submeshes = lod.get("submeshes", [])
    mesh_view_count = sum(len(sm.get("mesh_views", [])) for sm in submeshes)
    mesh_bb_min = lod.get("bounding_box_min", [0.0, 0.0, 0.0])
    mesh_bb_max = lod.get("bounding_box_max", [0.0, 0.0, 0.0])

    mesh_desc = (
        mesh_name
        + struct.pack("<I", vertex_buffer_idx)
        + struct.pack("<I", index_buffer_idx)
        + struct.pack("<I", len(submeshes))
        + struct.pack("<I", mesh_view_count)
        + struct.pack("<3f", *mesh_bb_min)
        + struct.pack("<3f", *mesh_bb_max)
    )

    if len(mesh_desc) != MESH_DESC_SIZE:
        raise RuntimeError(
            f"Mesh descriptor size mismatch: expected "
            f"{MESH_DESC_SIZE}, got {len(mesh_desc)}"
        )

    return mesh_desc


def create_submesh_descriptor(
    submesh: Dict[str, Any], simple_assets: List[Dict[str, Any]]
) -> bytes:
    """Create submesh descriptor from submesh specification."""
    sm_name = pack_name_string(submesh["name"], 64)
    mat_name = submesh["material"]

    # Find material asset key
    mat_key = None
    for asset_data in simple_assets:
        if asset_data["name"] == mat_name:
            mat_key = asset_data["key"]
            break

    if mat_key is None:
        raise ValueError(
            f"Material asset '{mat_name}' not found for "
            f"submesh '{submesh['name']}'"
        )

    if len(mat_key) != ASSET_KEY_SIZE:
        raise ValueError(
            f"Material asset key for '{mat_name}' must be "
            f"{ASSET_KEY_SIZE} bytes (got {len(mat_key)})"
        )

    mesh_views = submesh.get("mesh_views", [])
    sm_bb_min = submesh.get("bounding_box_min", [0.0, 0.0, 0.0])
    sm_bb_max = submesh.get("bounding_box_max", [0.0, 0.0, 0.0])

    submesh_desc = (
        sm_name
        + mat_key
        + struct.pack("<I", len(mesh_views))
        + struct.pack("<3f", *sm_bb_min)
        + struct.pack("<3f", *sm_bb_max)
    )

    if len(submesh_desc) != SUBMESH_DESC_SIZE:
        raise RuntimeError(
            f"Submesh descriptor size mismatch: expected "
            f"{SUBMESH_DESC_SIZE}, got {len(submesh_desc)}"
        )

    return submesh_desc


def create_mesh_view_descriptor(mesh_view: Dict[str, Any]) -> bytes:
    """Create mesh view descriptor from mesh view specification."""
    first_index = mesh_view.get("first_index", 0)
    index_count = mesh_view.get("index_count", 0)
    first_vertex = mesh_view.get("first_vertex", 0)
    vertex_count = mesh_view.get("vertex_count", 0)

    mesh_view_desc = struct.pack(
        "<4I", first_index, index_count, first_vertex, vertex_count
    )

    if len(mesh_view_desc) != MESH_VIEW_DESC_SIZE:
        raise RuntimeError(
            f"Mesh view descriptor size mismatch: expected "
            f"{MESH_VIEW_DESC_SIZE}, got {len(mesh_view_desc)}"
        )

    return mesh_view_desc


def write_asset_descriptors(
    file_obj: BinaryIO,
    material_assets: List[Dict[str, Any]],
    geometry_assets: List[Tuple[Dict[str, Any], bytes, int, int]],
    resource_index_map: Dict[str, Dict[str, int]],
) -> List[Dict[str, Any]]:
    """Write asset descriptors to file and return asset description list."""
    logger = get_logger()
    asset_descs = []

    with logger.section("Writing asset descriptors"):
        total_assets = len(material_assets) + len(geometry_assets)
        logger.step(
            f"Processing {total_assets} assets "
            f"({len(material_assets)} material, {len(geometry_assets)} geometry)"
        )

        # Align to asset descriptor boundary
        align_file(file_obj, DATA_ALIGNMENT)
        logger.verbose(
            f"Aligned to {DATA_ALIGNMENT}-byte boundary for asset descriptors"
        )

        # Write material asset descriptors
        if material_assets:
            logger.step(
                "Writing material asset descriptors", len(material_assets)
            )
            for i, asset_data in enumerate(material_assets):
                align_file(file_obj, asset_data["alignment"])
                offset = file_obj.tell()
                file_obj.write(asset_data["data"])
                asset_descs.append(
                    {
                        "key": asset_data["key"],
                        "type": asset_data["type"],
                        "offset": offset,
                        "size": len(asset_data["data"]),
                    }
                )
                logger.verbose(
                    f"  Asset '{asset_data['name']}': {len(asset_data['data'])} bytes at offset 0x{offset:08x}"
                )

        # Write geometry assets using extracted functions
        if geometry_assets:
            logger.step(
                "Writing geometry asset descriptors", len(geometry_assets)
            )
            for (
                asset,
                key,
                asset_type,
                alignment,
            ) in geometry_assets:
                asset_desc = write_geometry_asset(
                    file_obj,
                    asset,
                    key,
                    asset_type,
                    alignment,
                    material_assets,
                    resource_index_map,
                )
                asset_descs.append(asset_desc)

    return asset_descs


def write_geometry_asset(
    file_obj: BinaryIO,
    asset: Dict[str, Any],
    asset_key: bytes,
    asset_type: int,
    alignment: int,
    material_assets: List[Dict[str, Any]],
    resource_index_map: Dict[str, Dict[str, int]],
) -> Dict[str, Any]:
    """Write a single geometry asset and return its descriptor.

    Args:
        file_obj: File object to write to
        asset: Asset specification dictionary
        asset_key: Asset key bytes
        asset_type: Asset type integer
        alignment: Alignment requirement
        material_assets: List of material assets for material lookup
        resource_index_map: Resource index mapping

    Returns:
        Asset descriptor dictionary
    """
    logger = get_logger()

    with logger.section(f"Geometry asset '{asset['name']}'"):
        align_file(file_obj, alignment)
        offset = file_obj.tell()

        # Write geometry descriptor
        geometry_desc = create_geometry_asset_descriptor(
            asset, resource_index_map
        )
        file_obj.write(geometry_desc)

        asset_desc = {
            "key": asset_key,
            "type": asset_type,
            "offset": offset,
            "size": GEOMETRY_DESC_SIZE,
        }
        logger.verbose(
            f"Geometry descriptor: {GEOMETRY_DESC_SIZE} bytes at offset 0x{offset:08x}"
        )

        # Write LOD data
        lods = asset.get("lods", [])
        logger.step(f"Writing LOD data", len(lods))
        for lod_idx, lod in enumerate(lods):
            write_lod_data(
                file_obj, lod, lod_idx, material_assets, resource_index_map
            )

    return asset_desc


def write_lod_data(
    file_obj: BinaryIO,
    lod: Dict[str, Any],
    lod_idx: int,
    material_assets: List[Dict[str, Any]],
    resource_index_map: Dict[str, Dict[str, int]],
) -> None:
    """Write LOD data including mesh, submeshes, and mesh views.

    Args:
        file_obj: File object to write to
        lod: LOD specification dictionary
        lod_idx: LOD index for logging
        material_assets: List of material assets for material lookup
        resource_index_map: Resource index mapping
    """
    logger = get_logger()

    logger.verbose(f"  LOD {lod_idx+1}: '{lod['name']}'")

    # Write mesh descriptor
    mesh_desc = create_mesh_descriptor(lod, resource_index_map)
    file_obj.write(mesh_desc)

    # Write submesh descriptors
    submeshes = lod.get("submeshes", [])
    logger.verbose(f"    Writing {len(submeshes)} submeshes")

    for submesh in submeshes:
        write_submesh_data(file_obj, submesh, material_assets)


def write_submesh_data(
    file_obj: BinaryIO,
    submesh: Dict[str, Any],
    material_assets: List[Dict[str, Any]],
) -> None:
    """Write submesh data including mesh views.

    Args:
        file_obj: File object to write to
        submesh: Submesh specification dictionary
        material_assets: List of material assets for material lookup
    """
    logger = get_logger()

    # Write submesh descriptor
    submesh_desc = create_submesh_descriptor(submesh, material_assets)
    file_obj.write(submesh_desc)

    # Write mesh view descriptors
    mesh_views = submesh.get("mesh_views", [])
    logger.verbose(
        f"      Submesh '{submesh['name']}': {len(mesh_views)} mesh views"
    )

    for mesh_view in mesh_views:
        mesh_view_desc = create_mesh_view_descriptor(mesh_view)
        file_obj.write(mesh_view_desc)


def write_asset_directory(
    file_obj: BinaryIO, asset_descs: List[Dict[str, Any]]
) -> Tuple[int, List[AssetDirectoryEntry], int]:
    """Write asset directory to file and return directory information."""
    logger = get_logger()

    with logger.section("Writing asset directory"):
        logger.step(f"Writing directory entries", len(asset_descs))
        logger.verbose(f"Aligning to {TABLE_ALIGNMENT}-byte boundary")

        align_file(file_obj, TABLE_ALIGNMENT)
        directory_offset = file_obj.tell()
        directory_entries = []

        for i, desc in enumerate(asset_descs):
            # Calculate the offset of this directory entry
            current_entry_offset = directory_offset + (i * DIRECTORY_ENTRY_SIZE)

            entry = AssetDirectoryEntry(
                asset_key=desc["key"],
                asset_type=desc["type"],
                entry_offset=current_entry_offset,  # Offset to this directory entry
                desc_offset=desc[
                    "offset"
                ],  # Offset to the asset descriptor data
                desc_size=desc["size"],
            )
            directory_entries.append(entry)
            file_obj.write(entry.pack())
            logger.verbose(
                f"  Entry {i+1}: type={desc['type']}, offset=0x{desc['offset']:08x}, size={desc['size']}"
            )

        directory_size = len(directory_entries) * DIRECTORY_ENTRY_SIZE
        logger.verbose(
            f"Directory: {len(directory_entries)} entries of {DIRECTORY_ENTRY_SIZE} bytes = "
            f"{directory_size} bytes at offset 0x{directory_offset:08x}"
        )

    return directory_offset, directory_entries, directory_size


def write_footer(
    file_obj: BinaryIO,
    directory_offset: int,
    directory_size: int,
    asset_count: int,
    resource_region_offsets: Dict[str, Tuple[int, int]],
    resource_table_offsets: Dict[str, Tuple[int, int, int]],
) -> None:
    """Write PAK footer to file."""
    logger = get_logger()

    with logger.section("Writing PAK footer"):
        logger.step("Creating footer structure")
        footer = PakFooter(
            directory_offset=directory_offset,
            directory_size=directory_size,
            asset_count=asset_count,
            texture_region=ResourceRegion(*resource_region_offsets["texture"]),
            buffer_region=ResourceRegion(*resource_region_offsets["buffer"]),
            audio_region=ResourceRegion(*resource_region_offsets["audio"]),
            texture_table=ResourceTable(*resource_table_offsets["texture"]),
            buffer_table=ResourceTable(*resource_table_offsets["buffer"]),
            audio_table=ResourceTable(*resource_table_offsets["audio"]),
        )

        footer_offset = file_obj.tell()
        file_obj.write(footer.pack())

        logger.verbose(
            f"Footer: {FOOTER_SIZE} bytes at offset 0x{footer_offset:08x}"
        )
        logger.verbose(
            f"Directory reference: {asset_count} assets at offset 0x{directory_offset:08x}"
        )

        # Log resource summary in verbose mode
        for rtype in ["texture", "buffer", "audio"]:
            region_offset, region_size = resource_region_offsets[rtype]
            table_offset, table_count, table_entry_size = (
                resource_table_offsets[rtype]
            )
            if table_count > 0:
                logger.verbose(
                    f"{rtype.capitalize()} resources: {table_count} entries, "
                    f"data region: {region_size} bytes at 0x{region_offset:08x}, "
                    f"table: {table_count}×{table_entry_size} bytes at 0x{table_offset:08x}"
                )
            else:
                logger.verbose(f"{rtype.capitalize()} resources: none")


# === Main PAK Generation Function ===----------------------------------------//


def write_pak(
    spec: Dict[str, Any],
    output_path: Union[str, Path],
    base_dir: Union[str, Path],
) -> None:
    """Generate PAK file from specification.

    Args:
        spec: Parsed specification dictionary
        output_path: Output PAK file path
        base_dir: Base directory for resolving relative paths
    """
    logger = get_logger()
    output_path = Path(output_path)
    base_dir = Path(base_dir)

    version = spec.get("version", 1)
    content_version = spec.get("content_version", 0)
    resource_types = ["texture", "buffer", "audio"]

    with logger.section(f"Generating PAK file: {output_path.name}"):
        logger.step(
            f"PAK version: {version}, content version: {content_version}"
        )

        # Collect data from specification
        resource_result = collect_resources(spec, base_dir, resource_types)
        asset_result = collect_assets(spec, resource_result.index_map, base_dir)

        # Write PAK file atomically using temporary file
        with logger.section("Writing PAK file structure"):
            # Create temporary file in same directory to ensure atomic move
            temp_dir = output_path.parent
            temp_path = None

            try:
                # Create temporary file manually to avoid Windows locking issues

                temp_fd, temp_name = tempfile.mkstemp(
                    dir=temp_dir, suffix=".tmp"
                )
                temp_path = Path(temp_name)
                os.close(temp_fd)  # Close the file descriptor immediately

                with open(temp_path, "wb") as file_obj:
                    logger.step("Writing PAK header")
                    header = PakHeader(version, content_version)
                    file_obj.write(header.pack())
                    logger.verbose(f"Header: {HEADER_SIZE} bytes written")

                    # Write resource data blobs
                    resource_region_offsets, resource_data_offsets = (
                        write_resource_data_blobs(
                            file_obj, resource_types, resource_result.data_blobs
                        )
                    )

                    # Write resource tables
                    resource_table_offsets = write_resource_tables(
                        file_obj,
                        resource_types,
                        resource_result.data_blobs,
                        resource_result.desc_fields,
                        resource_data_offsets,
                    )

                    # Write asset descriptors
                    asset_descs = write_asset_descriptors(
                        file_obj,
                        asset_result.material_assets,
                        asset_result.geometry_assets,
                        resource_result.index_map,
                    )

                    # Write asset directory
                    directory_offset, directory_entries, directory_size = (
                        write_asset_directory(file_obj, asset_descs)
                    )

                    # Write footer (initially without hash)
                    write_footer(
                        file_obj,
                        directory_offset,
                        directory_size,
                        len(directory_entries),
                        resource_region_offsets,
                        resource_table_offsets,
                    )

                    final_size = file_obj.tell()

                # Calculate and update PAK hash
                with logger.section("Calculating PAK integrity hash"):
                    pak_crc32 = calculate_pak_crc32(temp_path)
                    logger.verbose(f"Calculated PAK CRC32: 0x{pak_crc32:08x}")

                    # Update the hash in the footer
                    with open(temp_path, "r+b") as file_obj:
                        # Seek to CRC32 field in footer (12 bytes from end: 4 CRC32 + 8 magic)
                        file_obj.seek(-12, 2)
                        file_obj.write(struct.pack("<I", pak_crc32))

                # Atomically move temp file to final location
                logger.step("Finalizing PAK file")
                if output_path.exists():
                    output_path.unlink()  # Remove existing file

                # On Windows, use shutil.move for better cross-platform compatibility
                shutil.move(str(temp_path), str(output_path))
                temp_path = None  # Successfully moved, don't try to clean up

                logger.success(
                    f"PAK file written: {final_size} bytes, {len(directory_entries)} assets"
                )
                logger.verbose(f"Final file size: {final_size} bytes")
                logger.verbose(
                    f"Assets: {len(asset_result.material_assets)} material + "
                    f"{len(asset_result.geometry_assets)} geometry = "
                    f"{len(directory_entries)} total"
                )

            except Exception as e:
                # Clean up temp file on error (only if it wasn't successfully moved)
                if temp_path and temp_path.exists():
                    try:
                        temp_path.unlink()
                    except OSError:
                        # If we can't delete the temp file, log a warning but don't fail
                        logger.warning(
                            f"Could not clean up temporary file: {temp_path}"
                        )
                raise e


# === CLI Entrypoint ===------------------------------------------------------//


def main() -> None:
    """Main CLI entrypoint for PAK file generation."""
    parser = argparse.ArgumentParser(
        description="Oxygen PAK file generator.",
        epilog="Generates binary PAK files from YAML/JSON specifications "
        "for use with the Oxygen Engine.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument("spec", help="YAML or JSON specification file")
    parser.add_argument(
        "output",
        nargs="?",
        help="Output PAK file path (default: <spec>.pak)",
    )
    parser.add_argument(
        "--verbose",
        "-v",
        action="store_true",
        help="Enable verbose output with detailed progress and timing",
    )
    parser.add_argument(
        "--dry-run",
        "-n",
        action="store_true",
        help="Validate specification and show what would be generated without writing output file",
    )
    parser.add_argument(
        "--force",
        "-f",
        action="store_true",
        help="Overwrite output file if it exists",
    )

    args = parser.parse_args()

    # Initialize logger with verbosity setting
    logger = Logger(verbose_mode=args.verbose)
    set_logger(logger)

    try:
        # Load and validate specification
        spec_path = Path(args.spec)
        if not spec_path.exists():
            logger.error(f"Specification file not found: {spec_path}")
            sys.exit(1)

        # Determine output path
        if args.output is None:
            output_path = spec_path.with_suffix(".pak")
        else:
            output_path = Path(args.output)

        # Only check/generate output file if not dry-run
        if not args.dry_run:
            # Check if output is same as input
            if output_path.resolve() == spec_path.resolve():
                logger.error(
                    "Output file must not be the same as the input specification file."
                )
                sys.exit(1)

            # Check if output file exists (unless --force)
            if output_path.exists() and not args.force:
                logger.error(
                    f"Output file already exists: {output_path}. Use --force to overwrite."
                )
                sys.exit(1)

        logger.info(f"Loading specification from {spec_path.name}")
        spec = load_spec(spec_path)
        logger.verbose(f"Loaded specification: {len(spec)} top-level keys")

        # Validate specification structure
        validate_specification(spec)

        # Determine base directory for relative paths
        base_dir = spec_path.parent
        logger.verbose(f"Base directory for relative paths: {base_dir}")

        if args.dry_run:
            logger.info("Performing dry-run validation...")
            # Collect data to validate everything but don't write
            resource_result = collect_resources(
                spec, base_dir, ["texture", "buffer", "audio"]
            )
            asset_result = collect_assets(
                spec, resource_result.index_map, base_dir
            )

            total_resources = sum(
                len(blobs) for blobs in resource_result.data_blobs.values()
            )
            total_assets = len(asset_result.material_assets) + len(
                asset_result.geometry_assets
            )

            logger.success(
                f"Dry-run completed: {total_resources} resources, {total_assets} assets would be written to {output_path.name}"
            )
        else:
            write_pak(spec, output_path, base_dir)
            logger.success("PAK file generation completed successfully")

    except KeyboardInterrupt:
        logger.error("Operation cancelled by user")
        sys.exit(130)
    except Exception as e:
        logger.error(f"{e}")
        if args.verbose:
            import traceback

            print("\nDetailed error information:", file=sys.stderr)
            traceback.print_exc()
        sys.exit(1)


if __name__ == "__main__":
    main()
