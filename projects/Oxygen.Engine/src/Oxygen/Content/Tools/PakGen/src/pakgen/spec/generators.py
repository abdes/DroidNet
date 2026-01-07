"""Procedural node generation for scene specifications.

This module implements layout algorithms for the 'generate' directive,
which allows a single template node to expand into multiple concrete nodes
with computed transforms.

Supported layouts:
- grid: 3D grid arrangement
- linear: Single line/row
- circle: Circular arrangement
- scatter: Randomized positions (Poisson disk sampling)
"""

from __future__ import annotations

import hashlib
import math
import random
from typing import Any, Dict, List, Optional, Tuple
from uuid import UUID

from ..packing.constants import MAX_GENERATED_NODES


class GeneratorError(Exception):
    """Error during node generation."""

    def __init__(self, code: str, message: str, path: str = "") -> None:
        super().__init__(message)
        self.code = code
        self.path = path


def _make_rotation_y_quaternion(radians: float) -> List[float]:
    """Return a quaternion for rotation around Y-axis in XYZW format."""
    half_angle = radians / 2.0
    return [0.0, math.sin(half_angle), 0.0, math.cos(half_angle)]


def _generate_deterministic_uuid(template_name: str, index: int) -> str:
    """Generate a deterministic UUID based on template name and index."""
    data = f"{template_name}:{index}".encode("utf-8")
    digest = hashlib.md5(data, usedforsecurity=False).digest()
    return str(UUID(bytes=digest))


def _get_float_param(params: Dict[str, Any], key: str, default: float) -> float:
    """Extract a float parameter with fallback."""
    val = params.get(key, default)
    if isinstance(val, (int, float)):
        return float(val)
    return default


def _get_int_param(params: Dict[str, Any], key: str, default: int) -> int:
    """Extract an int parameter with fallback."""
    val = params.get(key, default)
    if isinstance(val, int):
        return val
    return default


def _get_bool_param(params: Dict[str, Any], key: str, default: bool) -> bool:
    """Extract a bool parameter with fallback."""
    val = params.get(key, default)
    if isinstance(val, bool):
        return val
    return default


def _get_vec3_param(
    params: Dict[str, Any], key: str, default: Tuple[float, float, float]
) -> Tuple[float, float, float]:
    """Extract a 3-element vector parameter with fallback."""
    val = params.get(key, default)
    if isinstance(val, (list, tuple)) and len(val) >= 3:
        return (float(val[0]), float(val[1]), float(val[2]))
    if isinstance(val, (int, float)):
        # Uniform value
        return (float(val), float(val), float(val))
    return default


def _get_ivec3_param(
    params: Dict[str, Any], key: str, default: Tuple[int, int, int]
) -> Tuple[int, int, int]:
    """Extract a 3-element int vector parameter with fallback."""
    val = params.get(key, default)
    if isinstance(val, (list, tuple)) and len(val) >= 3:
        return (int(val[0]), int(val[1]), int(val[2]))
    if isinstance(val, int):
        # Uniform value
        return (val, val, val)
    return default


def expand_grid_layout(
    template: Dict[str, Any],
    params: Dict[str, Any],
    base_index: int,
    path: str,
) -> List[Dict[str, Any]]:
    """Expand a grid layout into concrete nodes.

    Parameters:
        template: The template node dict
        params: The 'grid' parameters dict
        base_index: Starting node index for parent references
        path: YAML path for error reporting

    Returns:
        List of expanded node dicts with computed transforms
    """
    count = _get_ivec3_param(params, "count", (1, 1, 1))
    spacing = _get_vec3_param(params, "spacing", (1.0, 1.0, 1.0))
    center = _get_bool_param(params, "center", True)
    offset = _get_vec3_param(params, "offset", (0.0, 0.0, 0.0))

    total_count = count[0] * count[1] * count[2]
    if total_count > MAX_GENERATED_NODES:
        raise GeneratorError(
            "E_GENERATE_LIMIT",
            f"Grid would generate {total_count} nodes (max: {MAX_GENERATED_NODES})",
            path,
        )

    # Calculate centering offset
    if center:
        center_offset = (
            -(count[0] - 1) * spacing[0] / 2.0,
            -(count[1] - 1) * spacing[1] / 2.0,
            -(count[2] - 1) * spacing[2] / 2.0,
        )
    else:
        center_offset = (0.0, 0.0, 0.0)

    template_name = template.get("name", "Node")
    template_parent = template.get("parent")
    nodes: List[Dict[str, Any]] = []
    idx = 0

    for iz in range(count[2]):
        for iy in range(count[1]):
            for ix in range(count[0]):
                # Calculate position
                x = ix * spacing[0] + center_offset[0] + offset[0]
                y = iy * spacing[1] + center_offset[1] + offset[1]
                z = iz * spacing[2] + center_offset[2] + offset[2]

                # Create node with TRS (translation, rotation, scale)
                node: Dict[str, Any] = {
                    "name": f"{template_name}_{idx}",
                    "translation": [x, y, z],
                    "rotation": [
                        0.0,
                        0.0,
                        0.0,
                        1.0,
                    ],  # Identity quaternion (XYZW)
                    "scale": [1.0, 1.0, 1.0],
                    "node_id": _generate_deterministic_uuid(template_name, idx),
                }

                # Copy inherited properties
                if template_parent is not None:
                    node["parent"] = template_parent

                nodes.append(node)
                idx += 1

    return nodes


def expand_linear_layout(
    template: Dict[str, Any],
    params: Dict[str, Any],
    base_index: int,
    path: str,
) -> List[Dict[str, Any]]:
    """Expand a linear layout into concrete nodes."""
    count = _get_int_param(params, "count", 10)
    direction = _get_vec3_param(params, "direction", (1.0, 0.0, 0.0))
    spacing_val = _get_float_param(params, "spacing", 1.0)
    start = _get_vec3_param(params, "start", (0.0, 0.0, 0.0))

    if count > MAX_GENERATED_NODES:
        raise GeneratorError(
            "E_GENERATE_LIMIT",
            f"Linear would generate {count} nodes (max: {MAX_GENERATED_NODES})",
            path,
        )

    # Normalize direction
    length = math.sqrt(
        direction[0] ** 2 + direction[1] ** 2 + direction[2] ** 2
    )
    if length < 1e-6:
        direction = (1.0, 0.0, 0.0)
    else:
        direction = (
            direction[0] / length,
            direction[1] / length,
            direction[2] / length,
        )

    template_name = template.get("name", "Node")
    template_parent = template.get("parent")
    nodes: List[Dict[str, Any]] = []

    for i in range(count):
        x = start[0] + i * spacing_val * direction[0]
        y = start[1] + i * spacing_val * direction[1]
        z = start[2] + i * spacing_val * direction[2]

        node: Dict[str, Any] = {
            "name": f"{template_name}_{i}",
            "translation": [x, y, z],
            "rotation": [0.0, 0.0, 0.0, 1.0],  # Identity quaternion (XYZW)
            "scale": [1.0, 1.0, 1.0],
            "node_id": _generate_deterministic_uuid(template_name, i),
        }

        if template_parent is not None:
            node["parent"] = template_parent

        nodes.append(node)

    return nodes


def expand_circle_layout(
    template: Dict[str, Any],
    params: Dict[str, Any],
    base_index: int,
    path: str,
) -> List[Dict[str, Any]]:
    """Expand a circle layout into concrete nodes."""
    count = _get_int_param(params, "count", 12)
    radius = _get_float_param(params, "radius", 5.0)
    center = _get_vec3_param(params, "center", (0.0, 0.0, 0.0))
    face_center = _get_bool_param(params, "face_center", False)

    if count > MAX_GENERATED_NODES:
        raise GeneratorError(
            "E_GENERATE_LIMIT",
            f"Circle would generate {count} nodes (max: {MAX_GENERATED_NODES})",
            path,
        )

    template_name = template.get("name", "Node")
    template_parent = template.get("parent")
    nodes: List[Dict[str, Any]] = []

    for i in range(count):
        angle = 2.0 * math.pi * i / count
        x = center[0] + radius * math.cos(angle)
        y = center[1]
        z = center[2] + radius * math.sin(angle)

        if face_center:
            # Rotate to face center (rotation around Y)
            rotation_angle = -angle + math.pi / 2
            rotation = _make_rotation_y_quaternion(rotation_angle)
        else:
            rotation = [0.0, 0.0, 0.0, 1.0]  # Identity quaternion (XYZW)

        node: Dict[str, Any] = {
            "name": f"{template_name}_{i}",
            "translation": [x, y, z],
            "rotation": rotation,
            "scale": [1.0, 1.0, 1.0],
            "node_id": _generate_deterministic_uuid(template_name, i),
        }

        if template_parent is not None:
            node["parent"] = template_parent

        nodes.append(node)

    return nodes


def expand_scatter_layout(
    template: Dict[str, Any],
    params: Dict[str, Any],
    base_index: int,
    path: str,
) -> List[Dict[str, Any]]:
    """Expand a scatter layout into concrete nodes with random positions."""
    count = _get_int_param(params, "count", 100)
    seed = _get_int_param(params, "seed", 42)
    bounds = params.get("bounds", {})
    min_bound = _get_vec3_param(bounds, "min", (-10.0, 0.0, -10.0))
    max_bound = _get_vec3_param(bounds, "max", (10.0, 5.0, 10.0))

    if count > MAX_GENERATED_NODES:
        raise GeneratorError(
            "E_GENERATE_LIMIT",
            f"Scatter would generate {count} nodes (max: {MAX_GENERATED_NODES})",
            path,
        )

    template_name = template.get("name", "Node")
    template_parent = template.get("parent")

    # Use seeded random for reproducibility
    rng = random.Random(seed)
    nodes: List[Dict[str, Any]] = []

    for i in range(count):
        x = rng.uniform(min_bound[0], max_bound[0])
        y = rng.uniform(min_bound[1], max_bound[1])
        z = rng.uniform(min_bound[2], max_bound[2])

        node: Dict[str, Any] = {
            "name": f"{template_name}_{i}",
            "translation": [x, y, z],
            "rotation": [0.0, 0.0, 0.0, 1.0],  # Identity quaternion (XYZW)
            "scale": [1.0, 1.0, 1.0],
            "node_id": _generate_deterministic_uuid(template_name, i),
        }

        if template_parent is not None:
            node["parent"] = template_parent

        nodes.append(node)

    return nodes


# Layout function registry
_LAYOUT_FUNCTIONS = {
    "grid": expand_grid_layout,
    "linear": expand_linear_layout,
    "circle": expand_circle_layout,
    "scatter": expand_scatter_layout,
}


def expand_node(
    template: Dict[str, Any],
    base_index: int,
    path: str,
) -> List[Dict[str, Any]]:
    """Expand a template node with 'generate' directive into concrete nodes.

    Parameters:
        template: The template node dict containing 'generate' field
        base_index: Starting node index for parent references
        path: YAML path for error reporting

    Returns:
        List of expanded node dicts, or [template] if no generate directive
    """
    generate = template.get("generate")
    if generate is None:
        return [template]

    if not isinstance(generate, dict):
        raise GeneratorError(
            "E_GENERATE_TYPE",
            "'generate' must be an object",
            path + ".generate",
        )

    layout = generate.get("layout")
    if layout is None:
        raise GeneratorError(
            "E_GENERATE_LAYOUT",
            "'generate.layout' is required",
            path + ".generate.layout",
        )

    if layout not in _LAYOUT_FUNCTIONS:
        raise GeneratorError(
            "E_GENERATE_LAYOUT",
            f"Unknown layout type: {layout}. "
            f"Supported: {', '.join(_LAYOUT_FUNCTIONS.keys())}",
            path + ".generate.layout",
        )

    # Get layout-specific parameters
    params = generate.get(layout, {})
    if not isinstance(params, dict):
        params = {}

    func = _LAYOUT_FUNCTIONS[layout]
    return func(template, params, base_index, path)


def expand_scene_nodes(
    scene: Dict[str, Any],
    path: str,
) -> None:
    """Expand all 'generate' directives in a scene's nodes list in-place.

    This modifies the scene dict directly, replacing template nodes
    with their expanded concrete nodes.

    Also generates renderable entries for nodes with geometry/material.
    """
    nodes = scene.get("nodes")
    if not isinstance(nodes, list):
        return

    expanded_nodes: List[Dict[str, Any]] = []
    renderables: List[Dict[str, Any]] = scene.get("renderables") or []
    if not isinstance(renderables, list):
        renderables = []

    for ni, node in enumerate(nodes):
        if not isinstance(node, dict):
            expanded_nodes.append(node)
            continue

        node_path = f"{path}.nodes[{ni}]"
        generated = expand_node(node, len(expanded_nodes), node_path)

        # Check if template has geometry/material for renderable generation
        geometry = node.get("geometry")
        material = node.get("material")
        generate_renderables = (
            geometry is not None and node.get("generate") is not None
        )

        for gen_node in generated:
            node_index = len(expanded_nodes)
            expanded_nodes.append(gen_node)

            # Auto-generate renderable for generated nodes with geometry
            if generate_renderables:
                renderable = {
                    "node_index": node_index,
                    "geometry": geometry,
                }
                if material is not None:
                    renderable["material_override"] = material
                renderables.append(renderable)

    scene["nodes"] = expanded_nodes
    if renderables:
        scene["renderables"] = renderables


__all__ = [
    "GeneratorError",
    "expand_node",
    "expand_scene_nodes",
    "expand_grid_layout",
    "expand_linear_layout",
    "expand_circle_layout",
    "expand_scatter_layout",
]
