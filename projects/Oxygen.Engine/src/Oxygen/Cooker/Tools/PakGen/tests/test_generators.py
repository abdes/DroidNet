"""Unit tests for the node generator module."""

from __future__ import annotations

import math
import pytest
from typing import Any, Dict, List

from pakgen.spec.generators import (
    GeneratorError,
    expand_node,
    expand_scene_nodes,
    expand_grid_layout,
    expand_linear_layout,
    expand_circle_layout,
    expand_scatter_layout,
)


class TestGridLayout:
    """Tests for grid layout generation."""

    def test_basic_grid_2x2x2(self) -> None:
        """Test basic 2x2x2 grid generates 8 nodes."""
        template = {"name": "Cube", "parent": 0}
        params = {"count": [2, 2, 2], "spacing": 1.0, "center": True}
        nodes = expand_grid_layout(template, params, 0, "test")

        assert len(nodes) == 8
        assert all(n["name"].startswith("Cube_") for n in nodes)
        assert all("translation" in n for n in nodes)
        assert all("rotation" in n for n in nodes)
        assert all("scale" in n for n in nodes)
        assert all("node_id" in n for n in nodes)
        assert all(n["parent"] == 0 for n in nodes)

    def test_grid_names_sequential(self) -> None:
        """Test grid node names are sequential."""
        template = {"name": "TestNode"}
        params = {"count": [3, 1, 1], "spacing": 1.0}
        nodes = expand_grid_layout(template, params, 0, "test")

        assert nodes[0]["name"] == "TestNode_0"
        assert nodes[1]["name"] == "TestNode_1"
        assert nodes[2]["name"] == "TestNode_2"

    def test_grid_centered_positions(self) -> None:
        """Test centered grid positions are symmetric around origin."""
        template = {"name": "Cube"}
        params = {"count": [3, 1, 1], "spacing": 2.0, "center": True}
        nodes = expand_grid_layout(template, params, 0, "test")

        # With count=3, spacing=2, centered: positions should be -2, 0, +2
        translations = [n["translation"] for n in nodes]
        x_positions = [t[0] for t in translations]

        assert x_positions[0] == pytest.approx(-2.0)
        assert x_positions[1] == pytest.approx(0.0)
        assert x_positions[2] == pytest.approx(2.0)

    def test_grid_not_centered(self) -> None:
        """Test non-centered grid starts at origin."""
        template = {"name": "Cube"}
        params = {"count": [3, 1, 1], "spacing": 2.0, "center": False}
        nodes = expand_grid_layout(template, params, 0, "test")

        translations = [n["translation"] for n in nodes]
        x_positions = [t[0] for t in translations]

        assert x_positions[0] == pytest.approx(0.0)
        assert x_positions[1] == pytest.approx(2.0)
        assert x_positions[2] == pytest.approx(4.0)

    def test_grid_with_offset(self) -> None:
        """Test grid with offset translation."""
        template = {"name": "Cube"}
        params = {
            "count": [1, 1, 1],
            "spacing": 1.0,
            "center": True,
            "offset": [10.0, 20.0, 30.0],
        }
        nodes = expand_grid_layout(template, params, 0, "test")

        translation = nodes[0]["translation"]
        assert translation[0] == pytest.approx(10.0)  # x
        assert translation[1] == pytest.approx(20.0)  # y
        assert translation[2] == pytest.approx(30.0)  # z

    def test_grid_per_axis_spacing(self) -> None:
        """Test grid with per-axis spacing values."""
        template = {"name": "Cube"}
        params = {
            "count": [2, 2, 2],
            "spacing": [1.0, 2.0, 3.0],
            "center": False,
        }
        nodes = expand_grid_layout(template, params, 0, "test")

        # Find the node at position (1, 1, 1) in grid coords
        # With center=False, it should be at (1*1, 1*2, 1*3) = (1, 2, 3)
        # Node ordering is Z-outer, Y-middle, X-inner, so node 7 = (1,1,1)
        translation = nodes[7]["translation"]
        assert translation[0] == pytest.approx(1.0)  # x
        assert translation[1] == pytest.approx(2.0)  # y
        assert translation[2] == pytest.approx(3.0)  # z

    def test_grid_deterministic_uuids(self) -> None:
        """Test that node UUIDs are deterministic."""
        template = {"name": "Cube"}
        params = {"count": [2, 1, 1]}

        nodes1 = expand_grid_layout(template, params, 0, "test")
        nodes2 = expand_grid_layout(template, params, 0, "test")

        assert nodes1[0]["node_id"] == nodes2[0]["node_id"]
        assert nodes1[1]["node_id"] == nodes2[1]["node_id"]
        # But different indices should have different IDs
        assert nodes1[0]["node_id"] != nodes1[1]["node_id"]


class TestLinearLayout:
    """Tests for linear layout generation."""

    def test_basic_linear(self) -> None:
        """Test basic linear layout."""
        template = {"name": "Post"}
        params = {"count": 5, "direction": [1.0, 0.0, 0.0], "spacing": 2.0}
        nodes = expand_linear_layout(template, params, 0, "test")

        assert len(nodes) == 5
        translations = [n["translation"] for n in nodes]
        x_positions = [t[0] for t in translations]

        assert x_positions == pytest.approx([0.0, 2.0, 4.0, 6.0, 8.0])

    def test_linear_custom_start(self) -> None:
        """Test linear layout with custom start position."""
        template = {"name": "Post"}
        params = {
            "count": 3,
            "direction": [0.0, 1.0, 0.0],
            "spacing": 1.0,
            "start": [5.0, 0.0, 0.0],
        }
        nodes = expand_linear_layout(template, params, 0, "test")

        translations = [n["translation"] for n in nodes]
        # All should have x=5, y varies
        assert all(t[0] == pytest.approx(5.0) for t in translations)
        y_positions = [t[1] for t in translations]
        assert y_positions == pytest.approx([0.0, 1.0, 2.0])

    def test_linear_normalizes_direction(self) -> None:
        """Test that direction vector is normalized."""
        template = {"name": "Post"}
        params = {
            "count": 2,
            "direction": [10.0, 0.0, 0.0],  # Non-unit vector
            "spacing": 3.0,
        }
        nodes = expand_linear_layout(template, params, 0, "test")

        translations = [n["translation"] for n in nodes]
        # Should still be spacing of 3 along normalized direction
        assert translations[0][0] == pytest.approx(0.0)
        assert translations[1][0] == pytest.approx(3.0)


class TestCircleLayout:
    """Tests for circle layout generation."""

    def test_basic_circle(self) -> None:
        """Test basic circle layout."""
        template = {"name": "Pillar"}
        params = {"count": 4, "radius": 5.0}
        nodes = expand_circle_layout(template, params, 0, "test")

        assert len(nodes) == 4

        # 4 nodes at 0°, 90°, 180°, 270° on XZ plane
        translations = [n["translation"] for n in nodes]
        positions = [(t[0], t[1], t[2]) for t in translations]

        # Node 0: angle=0 → (5, 0, 0)
        assert positions[0][0] == pytest.approx(5.0)
        assert positions[0][2] == pytest.approx(0.0, abs=1e-6)

        # Node 1: angle=90° → (0, 0, 5)
        assert positions[1][0] == pytest.approx(0.0, abs=1e-6)
        assert positions[1][2] == pytest.approx(5.0)

    def test_circle_with_center(self) -> None:
        """Test circle with offset center."""
        template = {"name": "Pillar"}
        params = {"count": 1, "radius": 1.0, "center": [10.0, 5.0, 0.0]}
        nodes = expand_circle_layout(template, params, 0, "test")

        translation = nodes[0]["translation"]
        assert translation[0] == pytest.approx(11.0)  # 10 + 1*cos(0)
        assert translation[1] == pytest.approx(5.0)  # y unchanged
        assert translation[2] == pytest.approx(0.0)  # 0 + 1*sin(0)


class TestScatterLayout:
    """Tests for scatter layout generation."""

    def test_basic_scatter(self) -> None:
        """Test basic scatter layout."""
        template = {"name": "Rock"}
        params = {
            "count": 10,
            "seed": 42,
            "bounds": {"min": [-5.0, 0.0, -5.0], "max": [5.0, 2.0, 5.0]},
        }
        nodes = expand_scatter_layout(template, params, 0, "test")

        assert len(nodes) == 10

        # All positions should be within bounds
        for n in nodes:
            t = n["translation"]
            assert -5.0 <= t[0] <= 5.0  # x
            assert 0.0 <= t[1] <= 2.0  # y
            assert -5.0 <= t[2] <= 5.0  # z

    def test_scatter_deterministic(self) -> None:
        """Test scatter is deterministic with same seed."""
        template = {"name": "Rock"}
        params = {"count": 5, "seed": 123}

        nodes1 = expand_scatter_layout(template, params, 0, "test")
        nodes2 = expand_scatter_layout(template, params, 0, "test")

        for n1, n2 in zip(nodes1, nodes2):
            assert n1["translation"] == n2["translation"]

    def test_scatter_different_seeds(self) -> None:
        """Test different seeds produce different results."""
        template = {"name": "Rock"}
        params1 = {"count": 5, "seed": 1}
        params2 = {"count": 5, "seed": 2}

        nodes1 = expand_scatter_layout(template, params1, 0, "test")
        nodes2 = expand_scatter_layout(template, params2, 0, "test")

        # At least one position should differ
        differs = False
        for n1, n2 in zip(nodes1, nodes2):
            if n1["translation"] != n2["translation"]:
                differs = True
                break
        assert differs


class TestExpandNode:
    """Tests for the expand_node dispatcher."""

    def test_no_generate_returns_original(self) -> None:
        """Test node without generate returns unchanged."""
        template = {"name": "Manual", "parent": 0, "transform": [1] * 16}
        result = expand_node(template, 0, "test")

        assert len(result) == 1
        assert result[0] is template

    def test_invalid_generate_type(self) -> None:
        """Test non-dict generate raises error."""
        template = {"name": "Bad", "generate": "not a dict"}

        with pytest.raises(GeneratorError) as exc:
            expand_node(template, 0, "test")

        assert exc.value.code == "E_GENERATE_TYPE"

    def test_missing_layout(self) -> None:
        """Test missing layout field raises error."""
        template = {"name": "Bad", "generate": {"grid": {"count": [2, 2, 2]}}}

        with pytest.raises(GeneratorError) as exc:
            expand_node(template, 0, "test")

        assert exc.value.code == "E_GENERATE_LAYOUT"

    def test_unknown_layout(self) -> None:
        """Test unknown layout type raises error."""
        template = {"name": "Bad", "generate": {"layout": "hexagonal"}}

        with pytest.raises(GeneratorError) as exc:
            expand_node(template, 0, "test")

        assert exc.value.code == "E_GENERATE_LAYOUT"
        assert "hexagonal" in str(exc.value)


class TestExpandSceneNodes:
    """Tests for full scene expansion."""

    def test_scene_expansion_in_place(self) -> None:
        """Test scene nodes are expanded in-place."""
        scene: Dict[str, Any] = {
            "name": "TestScene",
            "nodes": [
                {"name": "Root"},
                {
                    "name": "Cube",
                    "parent": 0,
                    "geometry": "CubeGeo",
                    "material": "Red",
                    "generate": {
                        "layout": "grid",
                        "grid": {"count": [2, 2, 1]},
                    },
                },
            ],
        }

        expand_scene_nodes(scene, "test")

        # Root + 4 generated cubes = 5 nodes
        assert len(scene["nodes"]) == 5
        assert scene["nodes"][0]["name"] == "Root"
        assert scene["nodes"][1]["name"] == "Cube_0"
        assert scene["nodes"][4]["name"] == "Cube_3"

    def test_scene_generates_renderables(self) -> None:
        """Test renderables are auto-generated for nodes with geometry."""
        scene: Dict[str, Any] = {
            "name": "TestScene",
            "nodes": [
                {"name": "Root"},
                {
                    "name": "Cube",
                    "parent": 0,
                    "geometry": "CubeGeo",
                    "material": "Blue",
                    "generate": {"layout": "linear", "linear": {"count": 3}},
                },
            ],
        }

        expand_scene_nodes(scene, "test")

        # Should have 3 renderables
        assert "renderables" in scene
        assert len(scene["renderables"]) == 3
        # Node indices should be 1, 2, 3 (Root is 0)
        indices = [r["node_index"] for r in scene["renderables"]]
        assert indices == [1, 2, 3]

    def test_mixed_manual_and_generated(self) -> None:
        """Test scene with both manual and generated nodes."""
        scene: Dict[str, Any] = {
            "name": "MixedScene",
            "nodes": [
                {"name": "Root"},
                {"name": "Camera", "parent": 0},  # Manual
                {
                    "name": "Cube",
                    "parent": 0,
                    "geometry": "Cube",
                    "generate": {
                        "layout": "grid",
                        "grid": {"count": [2, 1, 1]},
                    },
                },
                {"name": "Light", "parent": 0},  # Manual after generator
            ],
        }

        expand_scene_nodes(scene, "test")

        names = [n["name"] for n in scene["nodes"]]
        assert names == ["Root", "Camera", "Cube_0", "Cube_1", "Light"]


class TestGeneratorLimits:
    """Tests for safety limits."""

    def test_grid_exceeds_limit(self) -> None:
        """Test grid that would exceed node limit raises error."""
        template = {"name": "Cube"}
        # 100 * 100 * 100 = 1,000,000 > MAX_GENERATED_NODES
        params = {"count": [100, 100, 100]}

        with pytest.raises(GeneratorError) as exc:
            expand_grid_layout(template, params, 0, "test")

        assert exc.value.code == "E_GENERATE_LIMIT"
