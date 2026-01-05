# ===----------------------------------------------------------------------===#
# Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
# copy at https://opensource.org/licenses/BSD-3-Clause.
# SPDX-License-Identifier: BSD-3-Clause
# ===----------------------------------------------------------------------===#

import argparse
import struct
import sys
import uuid
from pathlib import Path
from typing import Any, Dict, List, Optional, Tuple

import numpy as np
import yaml
from pygltflib import GLTF2, Image

# Oxygen Engine Constants
VERTEX_STRIDE = 72  # 18 floats * 4 bytes
USAGE_VERTEX = 1
USAGE_INDEX = 2
TEXTURE_TYPE_2D = 3
FORMAT_RGBA8_UNORM = 30
FORMAT_R32_UINT = 10


_DEFAULT_EMPTY_BUFFER_NAME = "default_empty_buffer"
_DEFAULT_TEXTURE_NAME = "default_texture"
_DEFAULT_MATERIAL_NAME = "DefaultMaterial"


def generate_guid() -> str:
    return uuid.uuid4().hex


class GLBToPakSpec:
    def __init__(
        self,
        glb_path: Path,
        *,
        output_path: Optional[Path] = None,
        data_mode: str = "file",
    ):
        self.glb_path = glb_path
        self.output_path = output_path
        self.data_mode = data_mode
        self.gltf = GLTF2.load(glb_path)
        self.buffers: List[Dict[str, Any]] = []
        self.textures: List[Dict[str, Any]] = []
        self.assets: List[Dict[str, Any]] = []
        self.processed_materials = set()

        self._payload_dir: Optional[Path] = None
        if self.data_mode == "file":
            if self.output_path is None:
                raise ValueError(
                    "output_path is required when data_mode='file'"
                )
            # Write binary payloads next to the YAML output.
            self._payload_dir = self.output_path.with_suffix("")
            self._payload_dir = self._payload_dir.parent / (
                self._payload_dir.name + "_payload"
            )
            self._payload_dir.mkdir(parents=True, exist_ok=True)

        self.buffer_data = self.gltf.binary_blob()
        if self.buffer_data is None and self.gltf.buffers:
            # If not a GLB with a blob, try to load external buffer
            buf = self.gltf.buffers[0]
            if buf.uri:
                buf_path = glb_path.parent / buf.uri
                self.buffer_data = buf_path.read_bytes()

        self._ensure_v2_defaults()

    def _existing_names(self, entries: List[Dict[str, Any]]) -> set[str]:
        out: set[str] = set()
        for e in entries:
            n = e.get("name")
            if isinstance(n, str) and n:
                out.add(n)
        return out

    def _unique_resource_name(
        self,
        name: str,
        *,
        existing: set[str],
        reserved: set[str],
    ) -> str:
        base = name if isinstance(name, str) and name else "Resource"
        candidate = base
        suffix = 1
        while candidate in existing or candidate in reserved:
            candidate = f"{base}_{suffix}"
            suffix += 1
        existing.add(candidate)
        return candidate

    def _ensure_v2_defaults(self) -> None:
        # PakGen v3 requires:
        # - default empty buffer at index 0
        # - default texture at index 0
        # The converter may also emit materials that reference textures; having a
        # known index-0 fallback keeps specs robust.

        existing_buffers = self._existing_names(self.buffers)
        if _DEFAULT_EMPTY_BUFFER_NAME not in existing_buffers:
            self.buffers.insert(
                0,
                {
                    "name": _DEFAULT_EMPTY_BUFFER_NAME,
                    "data_hex": "",
                    "size": 0,
                    "usage": 0,
                },
            )

        existing_textures = self._existing_names(self.textures)
        if _DEFAULT_TEXTURE_NAME not in existing_textures:
            self.textures.insert(
                0,
                {
                    "name": _DEFAULT_TEXTURE_NAME,
                    "width": 1,
                    "height": 1,
                    "format": FORMAT_RGBA8_UNORM,
                    "texture_type": TEXTURE_TYPE_2D,
                    "compression_type": 0,
                    "data_hex": "ffffffff",
                },
            )

    def get_accessor_data(self, accessor_idx: int) -> Optional[np.ndarray]:
        """Decode accessor payload.

        Important: must respect bufferView.byteStride (interleaved attributes)
        and accessor.normalized (common for COLOR_0 and some tangents).
        """

        if accessor_idx is None:
            return None
        accessor = self.gltf.accessors[accessor_idx]

        # Sparse accessors are allowed by glTF, but this script does not
        # implement sparse patching yet.
        if getattr(accessor, "sparse", None):
            raise NotImplementedError(
                "Sparse accessors are not supported by glb_to_pak_spec.py"
            )

        if accessor.bufferView is None:
            return None

        buffer_view = self.gltf.bufferViews[accessor.bufferView]

        # Map GLTF component types to numpy dtypes
        component_type_map = {
            5120: np.int8,
            5121: np.uint8,
            5122: np.int16,
            5123: np.uint16,
            5125: np.uint32,
            5126: np.float32,
        }
        dtype = component_type_map.get(accessor.componentType)
        if dtype is None:
            return None

        # Map GLTF accessor types to component count
        type_map = {
            "SCALAR": 1,
            "VEC2": 2,
            "VEC3": 3,
            "VEC4": 4,
            "MAT2": 4,
            "MAT3": 9,
            "MAT4": 16,
        }

        count = int(accessor.count or 0)
        num_components = int(type_map.get(accessor.type, 1))
        if count <= 0:
            return None

        bv_offset = int(buffer_view.byteOffset or 0)
        acc_offset = int(accessor.byteOffset or 0)
        start = bv_offset + acc_offset

        itemsize = np.dtype(dtype).itemsize
        element_size = num_components * itemsize
        stride = int(getattr(buffer_view, "byteStride", 0) or 0)
        if stride == 0:
            stride = element_size

        # Fast-path for tightly packed arrays.
        if stride == element_size:
            byte_length = count * element_size
            data = self.buffer_data[start : start + byte_length]
            arr = np.frombuffer(data, dtype=dtype)
            arr = arr.reshape((count, num_components))
        else:
            # Interleaved buffer view: gather elements respecting stride.
            out = np.empty((count, num_components), dtype=dtype)
            for i in range(count):
                o = start + i * stride
                chunk = self.buffer_data[o : o + element_size]
                out[i, :] = np.frombuffer(
                    chunk, dtype=dtype, count=num_components
                )
            arr = out

        # Handle normalized integer accessors.
        if bool(getattr(accessor, "normalized", False)) and dtype != np.float32:
            arr_f = arr.astype(np.float32)
            if dtype == np.int8:
                arr_f /= 127.0
            elif dtype == np.uint8:
                arr_f /= 255.0
            elif dtype == np.int16:
                arr_f /= 32767.0
            elif dtype == np.uint16:
                arr_f /= 65535.0
            else:
                # uint32 normalized is uncommon; leave as-is.
                pass
            arr = arr_f

        return arr

    def _compute_vertex_normals(
        self, positions: np.ndarray, indices: np.ndarray
    ) -> np.ndarray:
        """Compute per-vertex normals for a triangle list."""
        if positions is None or len(positions) == 0:
            return np.zeros((0, 3), dtype=np.float32)

        vcount = int(positions.shape[0])
        normals = np.zeros((vcount, 3), dtype=np.float32)

        # indices comes in as Nx1 for SCALAR; flatten to 1D
        idx = indices.reshape(-1).astype(np.int64, copy=False)
        tri_count = idx.size // 3
        if tri_count == 0:
            # Fallback: default up
            normals[:, 2] = 1.0
            return normals

        idx = idx[: tri_count * 3].reshape((tri_count, 3))
        # Guard against OOB indices in malformed assets
        if idx.max(initial=0) >= vcount or idx.min(initial=0) < 0:
            normals[:, 2] = 1.0
            return normals

        p0 = positions[idx[:, 0], :]
        p1 = positions[idx[:, 1], :]
        p2 = positions[idx[:, 2], :]
        face_n = np.cross((p1 - p0), (p2 - p0)).astype(np.float32)

        # Accumulate face normals to vertices
        for k in range(3):
            np.add.at(normals, idx[:, k], face_n)

        # Normalize; avoid division by zero
        lens = np.linalg.norm(normals, axis=1)
        good = lens > 1e-20
        normals[good] /= lens[good][:, None]
        normals[~good, 2] = 1.0
        return normals

    def process_mesh(self, mesh_idx: int) -> str:
        mesh = self.gltf.meshes[mesh_idx]
        mesh_name = mesh.name or f"Mesh_{mesh_idx}"

        # For simplicity, we'll combine all primitives into one vertex/index buffer pair
        # In a real engine, you might want one per primitive or LOD.
        all_vertices = []
        all_indices = []
        submeshes = []

        current_vertex_offset = 0
        current_index_offset = 0

        for i, prim in enumerate(mesh.primitives):
            # Extract attributes
            pos = self.get_accessor_data(prim.attributes.POSITION)
            norm = self.get_accessor_data(prim.attributes.NORMAL)
            uv = self.get_accessor_data(prim.attributes.TEXCOORD_0)
            tangent = self.get_accessor_data(prim.attributes.TANGENT)
            color = self.get_accessor_data(prim.attributes.COLOR_0)

            indices = self.get_accessor_data(prim.indices)
            if indices is None:
                # Generate sequential indices if missing
                indices = np.arange(len(pos), dtype=np.uint32).reshape(-1, 1)

            # Some assets omit normals; compute them so lighting doesn't break.
            if norm is None:
                norm = self._compute_vertex_normals(
                    pos.astype(np.float32, copy=False),
                    indices.astype(np.uint32, copy=False),
                )

            vertex_count = len(pos)
            index_count = len(indices)

            # Interleave into Oxygen Vertex format
            # struct Vertex { vec3 pos, vec3 norm, vec2 uv, vec3 tang, vec3 bitang, vec4 color }
            interleaved = np.zeros((vertex_count, 18), dtype=np.float32)

            interleaved[:, 0:3] = pos
            if norm is not None:
                interleaved[:, 3:6] = norm
            if uv is not None:
                interleaved[:, 6:8] = uv
            if tangent is not None:
                interleaved[:, 8:11] = tangent[:, 0:3]
                # Bitangent = cross(normal, tangent) * tangent.w
                if norm is not None:
                    bitangents = (
                        np.cross(norm, tangent[:, 0:3]) * tangent[:, 3:4]
                    )
                    interleaved[:, 11:14] = bitangents
            if color is not None:
                if color.shape[1] == 3:
                    interleaved[:, 14:17] = color
                    interleaved[:, 17] = 1.0
                else:
                    interleaved[:, 14:18] = color
            else:
                interleaved[:, 14:18] = 1.0  # Default white

            all_vertices.append(interleaved)

            # Keep winding order from GLTF (CCW) as Oxygen also uses CCW
            all_indices.append(indices.reshape(-1))

            # Material name
            mat_name = "DefaultMaterial"
            if prim.material is not None:
                mat_name = (
                    self.gltf.materials[prim.material].name
                    or f"Material_{prim.material}"
                )

            self.processed_materials.add(mat_name)

            # Calculate bounding box for submesh
            bbox_min = pos.min(axis=0).tolist()
            bbox_max = pos.max(axis=0).tolist()

            submeshes.append(
                {
                    "name": f"Primitive_{i}",
                    "material": mat_name,
                    "bounding_box_min": bbox_min,
                    "bounding_box_max": bbox_max,
                    "mesh_views": [
                        {
                            "first_index": current_index_offset,
                            "index_count": index_count,
                            "first_vertex": current_vertex_offset,
                            "vertex_count": vertex_count,
                        }
                    ],
                }
            )

            current_vertex_offset += vertex_count
            current_index_offset += index_count

        # Create buffers
        vb_data = np.concatenate(all_vertices).tobytes()
        ib_data = np.concatenate(all_indices).astype(np.uint32).tobytes()

        vb_name = f"{mesh_name}_vb"
        ib_name = f"{mesh_name}_ib"

        # Avoid clobbering reserved v2 names.
        reserved_buffers = {_DEFAULT_EMPTY_BUFFER_NAME}
        existing_buffers = self._existing_names(self.buffers)
        vb_name = self._unique_resource_name(
            vb_name, existing=existing_buffers, reserved=reserved_buffers
        )
        ib_name = self._unique_resource_name(
            ib_name, existing=existing_buffers, reserved=reserved_buffers
        )

        def _emit_blob(
            resource_list: List[Dict[str, Any]],
            *,
            name: str,
            fields: Dict[str, Any],
            payload: bytes,
            ext: str,
        ) -> None:
            if self.data_mode == "hex":
                resource_list.append(
                    {**fields, "name": name, "data_hex": payload.hex()}
                )
                return
            if self.data_mode != "file":
                raise ValueError(f"Unknown data_mode: {self.data_mode}")
            if self._payload_dir is None:
                raise RuntimeError("payload dir not initialized")

            safe_name = "".join(
                c if c.isalnum() or c in "._-" else "_" for c in name
            )
            path = self._payload_dir / f"{safe_name}{ext}"
            path.write_bytes(payload)
            rel = (
                path.relative_to(self.output_path.parent).as_posix()
                if self.output_path
                else path.as_posix()
            )
            resource_list.append({**fields, "name": name, "file": rel})

        _emit_blob(
            self.buffers,
            name=vb_name,
            fields={
                "usage": USAGE_VERTEX,
                "stride": VERTEX_STRIDE,
            },
            payload=vb_data,
            ext=".bin",
        )

        _emit_blob(
            self.buffers,
            name=ib_name,
            fields={
                "usage": USAGE_INDEX,
                "stride": 0,
                "format": FORMAT_R32_UINT,
            },
            payload=ib_data,
            ext=".bin",
        )

        # Calculate overall bounding box
        all_pos = np.concatenate([v[:, 0:3] for v in all_vertices])
        geo_bbox_min = all_pos.min(axis=0).tolist()
        geo_bbox_max = all_pos.max(axis=0).tolist()

        # Create Geometry Asset
        geo_asset = {
            "type": "geometry",
            "name": mesh_name,
            "asset_key": generate_guid(),
            "bounding_box_min": geo_bbox_min,
            "bounding_box_max": geo_bbox_max,
            "lods": [
                {
                    "name": "lod0",
                    "mesh_type": 1,
                    "vertex_buffer": vb_name,
                    "index_buffer": ib_name,
                    "bounding_box_min": geo_bbox_min,
                    "bounding_box_max": geo_bbox_max,
                    "submeshes": submeshes,
                }
            ],
        }
        self.assets.append(geo_asset)
        return mesh_name

    def process_materials(self):
        defined_materials = set()
        for i, mat in enumerate(self.gltf.materials):
            mat_name = mat.name or f"Material_{i}"
            defined_materials.add(mat_name)

            pbr = mat.pbrMetallicRoughness
            base_color = pbr.baseColorFactor if pbr else [1.0, 1.0, 1.0, 1.0]

            asset = {
                "type": "material",
                "name": mat_name,
                "asset_key": generate_guid(),
                "material_domain": 1,  # kOpaque
                "flags": 1,  # kMaterialFlag_NoTextureSampling
                "shader_stages": 0,
                "base_color": base_color,
                "normal_scale": 1.0,
                "metalness": pbr.metallicFactor if pbr else 0.0,
                "roughness": pbr.roughnessFactor if pbr else 1.0,
                "ambient_occlusion": 1.0,
                "texture_refs": {},
            }

            if pbr and pbr.baseColorTexture:
                tex_idx = pbr.baseColorTexture.index
                tex_name = self.process_texture(tex_idx)
                asset["texture_refs"]["base_color_texture"] = tex_name
                # If we have a texture, clear the NoTextureSampling flag
                asset["flags"] = 0

            if mat.normalTexture:
                tex_idx = mat.normalTexture.index
                tex_name = self.process_texture(tex_idx)
                asset["texture_refs"]["normal_texture"] = tex_name
                asset["flags"] = 0

            self.assets.append(asset)

        # PakGen convenience: always provide a DefaultMaterial, even if the
        # source asset didn't define one.
        if _DEFAULT_MATERIAL_NAME not in defined_materials:
            self.assets.append(
                {
                    "type": "material",
                    "name": _DEFAULT_MATERIAL_NAME,
                    "asset_key": generate_guid(),
                    "material_domain": 1,  # kOpaque
                    "flags": 1,  # kMaterialFlag_NoTextureSampling
                    "shader_stages": 0,
                    "base_color": [1.0, 1.0, 1.0, 1.0],
                    "normal_scale": 1.0,
                    "metalness": 0.0,
                    "roughness": 1.0,
                    "ambient_occlusion": 1.0,
                    "texture_refs": {},
                }
            )

    def process_texture(self, tex_idx: int) -> str:
        texture = self.gltf.textures[tex_idx]
        image_idx = texture.source
        image = self.gltf.images[image_idx]

        tex_name = image.name or f"Texture_{image_idx}"

        # Avoid colliding with the required v2 default texture name.
        if tex_name == _DEFAULT_TEXTURE_NAME:
            tex_name = f"{tex_name}_gltf"

        # Ensure uniqueness against existing + reserved names.
        reserved_textures = {_DEFAULT_TEXTURE_NAME}
        existing_textures = self._existing_names(self.textures)
        tex_name = self._unique_resource_name(
            tex_name, existing=existing_textures, reserved=reserved_textures
        )

        # Extract image data
        from PIL import Image as PILImage
        import io

        img_data = None
        if image.uri and not image.uri.startswith("data:"):
            img_path = self.glb_path.parent / image.uri
            img_data = img_path.read_bytes()
        else:
            # Handle embedded or GLB blob
            if image.bufferView is not None:
                bv = self.gltf.bufferViews[image.bufferView]
                start = bv.byteOffset
                end = start + bv.byteLength
                img_data = self.buffer_data[start:end]
            elif image.uri and image.uri.startswith("data:"):
                import base64

                _, data = image.uri.split(",", 1)
                img_data = base64.b64decode(data)

        if img_data:
            with PILImage.open(io.BytesIO(img_data)) as pil_img:
                pil_img = pil_img.convert("RGBA")
                width, height = pil_img.size
                raw_data = pil_img.tobytes()

                if self.data_mode == "hex":
                    self.textures.append(
                        {
                            "name": tex_name,
                            "width": width,
                            "height": height,
                            "texture_type": TEXTURE_TYPE_2D,
                            "compression_type": 0,
                            "format": FORMAT_RGBA8_UNORM,
                            "data_hex": raw_data.hex(),
                        }
                    )
                else:
                    if self._payload_dir is None:
                        raise RuntimeError("payload dir not initialized")
                    safe_name = "".join(
                        c if c.isalnum() or c in "._-" else "_"
                        for c in tex_name
                    )
                    path = self._payload_dir / f"{safe_name}.rgba8"
                    path.write_bytes(raw_data)
                    rel = (
                        path.relative_to(self.output_path.parent).as_posix()
                        if self.output_path
                        else path.as_posix()
                    )
                    self.textures.append(
                        {
                            "name": tex_name,
                            "width": width,
                            "height": height,
                            "texture_type": TEXTURE_TYPE_2D,
                            "compression_type": 0,
                            "format": FORMAT_RGBA8_UNORM,
                            "file": rel,
                        }
                    )

        return tex_name

    def process_scene(self):
        scene = self.gltf.scenes[self.gltf.scene or 0]
        scene_name = scene.name or "MainScene"

        nodes = []
        renderables = []

        # Oxygen uses Z-up. glTF is Y-up. Apply a global +90° rotation around X
        # at a single synthetic root so all child nodes are converted.
        q_fix = [0.7071067811865475, 0.0, 0.0, 0.7071067811865476]

        # Create a single Root node at index 0, matching PakGen golden specs.
        nodes.append(
            {
                "name": "Root",
                "parent": 0,
                "node_id": generate_guid(),
                "flags": 0,
                "translation": [0.0, 0.0, 0.0],
                "rotation": q_fix,
                "scale": [1.0, 1.0, 1.0],
            }
        )

        # Flatten hierarchy for simplicity in this script
        def process_node(node_idx: int, parent_idx: int = -1):
            node = self.gltf.nodes[node_idx]
            node_name = node.name or f"Node_{node_idx}"

            current_idx = len(nodes)

            # Transform
            translation = node.translation or [0.0, 0.0, 0.0]
            rotation = node.rotation or [0.0, 0.0, 0.0, 1.0]
            scale = node.scale or [1.0, 1.0, 1.0]

            nodes.append(
                {
                    "name": node_name,
                    "parent": parent_idx if parent_idx != -1 else current_idx,
                    "node_id": generate_guid(),
                    "flags": 0,
                    "translation": translation,
                    "rotation": rotation,
                    "scale": scale,
                }
            )

            if node.mesh is not None:
                mesh_name = self.process_mesh(node.mesh)
                renderables.append(
                    {
                        "node_index": current_idx,
                        "geometry": mesh_name,
                        "visible": True,
                    }
                )

            for child_idx in node.children or []:
                process_node(child_idx, current_idx)

        for root_node_idx in scene.nodes:
            process_node(root_node_idx, 0)

        self.assets.append(
            {
                "type": "scene",
                "name": scene_name,
                "asset_key": generate_guid(),
                "nodes": nodes,
                "renderables": renderables,
            }
        )

    def generate_spec(self) -> Dict[str, Any]:
        self.process_scene()
        self.process_materials()

        return {
            "version": 3,
            "content_version": 1,
            "buffers": self.buffers,
            "textures": self.textures,
            "audios": [],
            "assets": self.assets,
        }


def main():
    parser = argparse.ArgumentParser(
        description="Convert GLB to Oxygen PakGen YAML spec"
    )
    parser.add_argument("input", type=Path, help="Input GLB file")
    parser.add_argument("output", type=Path, help="Output YAML file")
    parser.add_argument(
        "--data-mode",
        choices=["file", "hex"],
        default="file",
        help="How to store binary payloads: 'file' writes external .bin/.rgba8 files (fast), 'hex' embeds data_hex in YAML (slow).",
    )
    args = parser.parse_args()

    converter = GLBToPakSpec(
        args.input, output_path=args.output, data_mode=args.data_mode
    )
    spec = converter.generate_spec()

    with open(args.output, "w") as f:
        yaml.dump(spec, f, sort_keys=False)

    print(f"Successfully generated {args.output}")


if __name__ == "__main__":
    main()
