"""Writes the render mesh of the PhysicsDomains terrain patch.

The mesh is drawn from the height field the collider cooks from, sample for
sample: Content/Scenes/PhysicsDomains/terrain.shape.json. Its triangles split
each grid cell along the diagonal the Jolt height field uses, and leave out
every triangle that touches a hole, so what is drawn is what bodies rest on.
From the examples project root:

    python Tools/Generators/TerrainPatch.py

writes Content/SourceMedia/DCC/PhysicsDomains/terrain_{vb,ib}.bin and
Content/Scenes/PhysicsDomains/terrain.ogeo.json.
"""

import json
import math
import pathlib
import struct

ROOT = pathlib.Path(__file__).resolve().parents[2]
SCENE = ROOT / "Content" / "Scenes" / "PhysicsDomains"
MEDIA = ROOT / "Content" / "SourceMedia" / "DCC" / "PhysicsDomains"
SURFACE_ID = "146b1397-c3e0-bc20-fce3-c55e470b4086"

# One vertex as the engine stores it (data::Vertex, 80 bytes): position,
# normal, uv0, tangent, bitangent, colour, uv1.
VERTEX = struct.Struct("<20f")

Vec3 = tuple[float, float, float]


def normalized(v: Vec3) -> Vec3:
    length = math.sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2])
    return (v[0] / length, v[1] / length, v[2] / length)


def main() -> None:
    shape = json.loads((SCENE / "terrain.shape.json").read_text(encoding="utf-8"))
    count: int = shape["sample_count"]
    spacing_x, spacing_y = shape.get("sample_spacing", [1.0, 1.0])
    heights: list[float | None] = shape["heights"]

    def height(row: int, column: int) -> float | None:
        return heights[row * count + column]

    def solid(row: int, column: int) -> float:
        # A hole's own vertex takes its nearest solid neighbour's height; no
        # drawn triangle uses it.
        value = height(row, column)
        if value is not None:
            return value
        around = (
            (row - 1, column),
            (row + 1, column),
            (row, column - 1),
            (row, column + 1),
        )
        neighbours = [
            height(r, c) for r, c in around if 0 <= r < count and 0 <= c < count
        ]
        return next(h for h in neighbours if h is not None)

    vertices = bytearray()
    low: list[float] = [math.inf] * 3
    high: list[float] = [-math.inf] * 3
    for row in range(count):
        for column in range(count):
            z = solid(row, column)
            # Central differences, one-sided at the border.
            west, east = max(column - 1, 0), min(column + 1, count - 1)
            south, north = max(row - 1, 0), min(row + 1, count - 1)
            dx = (solid(row, east) - solid(row, west)) / (spacing_x * (east - west))
            dy = (solid(north, column) - solid(south, column)) / (
                spacing_y * (north - south)
            )
            normal = normalized((-dx, -dy, 1.0))
            tangent = normalized((1.0, 0.0, dx))
            bitangent = normalized((0.0, 1.0, dy))
            position = (column * spacing_x, row * spacing_y, z)
            uv = (column / (count - 1), row / (count - 1))
            vertices += VERTEX.pack(
                *position, *normal, *uv, *tangent, *bitangent, 1.0, 1.0, 1.0, 1.0, *uv
            )
            for axis in range(3):
                low[axis] = min(low[axis], position[axis])
                high[axis] = max(high[axis], position[axis])

    # Each cell splits along its (column, row + 1)-(column + 1, row) diagonal,
    # the one the cooked Jolt grid uses once its rows are stored last first.
    # Triangles run counter-clockwise seen from +Z.
    indices: list[int] = []
    for row in range(count - 1):
        for column in range(count - 1):
            a = (row + 1) * count + column  # (column, row + 1)
            b = row * count + column + 1  # (column + 1, row)
            c = row * count + column  # (column, row)
            d = (row + 1) * count + column + 1  # (column + 1, row + 1)
            for triangle in ((a, c, b), (a, b, d)):
                if all(heights[i] is not None for i in triangle):
                    indices.extend(triangle)

    MEDIA.mkdir(parents=True, exist_ok=True)
    (MEDIA / "terrain_vb.bin").write_bytes(bytes(vertices))
    (MEDIA / "terrain_ib.bin").write_bytes(struct.pack(f"<{len(indices)}I", *indices))

    bounds = {"min": [round(v, 6) for v in low], "max": [round(v, 6) for v in high]}
    geometry = {
        "$schema": "oxygen.geometry-descriptor.schema.json",
        "name": "terrain",
        "bounds": bounds,
        "lods": [
            {
                "name": "LOD0",
                "mesh_type": "standard",
                "bounds": bounds,
                "buffers": {"vb_ref": "terrain_vb.obuf", "ib_ref": "terrain_ib.obuf"},
                "submeshes": [
                    {
                        "name": "terrain_surface",
                        "surface_id": SURFACE_ID,
                        "material_ref": "terrain_moss.omat",
                        "bounds": bounds,
                        "views": [{"view_ref": "__all__"}],
                    }
                ],
            }
        ],
    }
    (SCENE / "terrain.ogeo.json").write_text(
        json.dumps(geometry, indent=2) + "\n", encoding="utf-8", newline="\n"
    )


if __name__ == "__main__":
    main()
