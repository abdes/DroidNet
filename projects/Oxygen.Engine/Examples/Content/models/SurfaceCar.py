"""Writes SurfaceCar.gltf, for testing geometry surfaces.

One mesh whose primitives use five materials. Two bumpers share the Chrome material,
so the importer merges them into one surface; the cabin glass encloses the seats, so
hiding Glass shows Interior. Run it from this directory:

    python SurfaceCar.py SurfaceCar.gltf
"""

import base64
import json
import pathlib
import struct
import sys

# (material, [(min, max), ...]) in glTF space: Y up, metres.
PARTS = [
    ("Paint", [((-1.0, 0.25, -0.5), (1.0, 0.7, 0.5))]),
    ("Glass", [((-0.45, 0.7, -0.45), (0.55, 1.1, 0.45))]),
    ("Interior", [((-0.3, 0.7, -0.35), (0.3, 0.95, 0.35))]),
    (
        "Rubber",
        [
            ((x - 0.2, 0.0, z - 0.08), (x + 0.2, 0.4, z + 0.08))
            for x in (-0.65, 0.65)
            for z in (-0.55, 0.55)
        ],
    ),
    ("Chrome", [((1.0, 0.25, -0.45), (1.08, 0.4, 0.45))]),
    ("Chrome", [((-1.08, 0.25, -0.45), (-1.0, 0.4, 0.45))]),
]

MATERIALS = {
    "Paint": ([0.75, 0.05, 0.05, 1.0], 0.3, 0.35),
    "Glass": ([0.55, 0.75, 0.9, 1.0], 0.0, 0.05),
    "Interior": ([0.35, 0.2, 0.1, 1.0], 0.0, 0.8),
    "Rubber": ([0.03, 0.03, 0.03, 1.0], 0.0, 0.9),
    "Chrome": ([0.9, 0.9, 0.9, 1.0], 1.0, 0.15),
}

FACES = [  # normal, then the two axes spanning the face, as (axis, sign)
    ((1, 0, 0), 2, 1),
    ((-1, 0, 0), 2, 1),
    ((0, 1, 0), 0, 2),
    ((0, -1, 0), 0, 2),
    ((0, 0, 1), 0, 1),
    ((0, 0, -1), 0, 1),
]


def box(lo, hi):
    positions, normals, uvs, indices = [], [], [], []
    for normal, u_axis, v_axis in FACES:
        axis = next(i for i in range(3) if normal[i] != 0)
        base = len(positions)
        for u, v in ((0, 0), (1, 0), (1, 1), (0, 1)):
            p = [0.0, 0.0, 0.0]
            p[axis] = hi[axis] if normal[axis] > 0 else lo[axis]
            p[u_axis] = hi[u_axis] if u else lo[u_axis]
            p[v_axis] = hi[v_axis] if v else lo[v_axis]
            positions.append(p)
            normals.append(list(map(float, normal)))
            uvs.append([float(u), float(v)])
        quad = [base, base + 1, base + 2, base, base + 2, base + 3]
        # Keep counter-clockwise winding seen from outside.
        e1 = [positions[base + 1][i] - positions[base][i] for i in range(3)]
        e2 = [positions[base + 2][i] - positions[base][i] for i in range(3)]
        cross = [
            e1[1] * e2[2] - e1[2] * e2[1],
            e1[2] * e2[0] - e1[0] * e2[2],
            e1[0] * e2[1] - e1[1] * e2[0],
        ]
        if sum(cross[i] * normal[i] for i in range(3)) < 0:
            quad = [base, base + 2, base + 1, base, base + 3, base + 2]
        indices += quad
    return positions, normals, uvs, indices


def main(out: pathlib.Path) -> None:
    blob = bytearray()
    views, accessors, primitives = [], [], []
    names = list(MATERIALS)

    def add(
        data: bytes, target: int, count: int, kind: str, component: int, bounds=None
    ) -> int:
        while len(blob) % 4:
            blob.append(0)
        views.append(
            {
                "buffer": 0,
                "byteOffset": len(blob),
                "byteLength": len(data),
                "target": target,
            }
        )
        blob.extend(data)
        accessor = {
            "bufferView": len(views) - 1,
            "componentType": component,
            "count": count,
            "type": kind,
        }
        if bounds:
            accessor["min"], accessor["max"] = bounds
        accessors.append(accessor)
        return len(accessors) - 1

    for material, boxes in PARTS:
        positions, normals, uvs, indices = [], [], [], []
        for lo, hi in boxes:
            p, n, t, i = box(lo, hi)
            indices += [index + len(positions) for index in i]
            positions += p
            normals += n
            uvs += t
        bounds = (
            [min(p[i] for p in positions) for i in range(3)],
            [max(p[i] for p in positions) for i in range(3)],
        )
        primitives.append(
            {
                "attributes": {
                    "POSITION": add(
                        b"".join(struct.pack("<3f", *p) for p in positions),
                        34962,
                        len(positions),
                        "VEC3",
                        5126,
                        bounds,
                    ),
                    "NORMAL": add(
                        b"".join(struct.pack("<3f", *n) for n in normals),
                        34962,
                        len(normals),
                        "VEC3",
                        5126,
                    ),
                    "TEXCOORD_0": add(
                        b"".join(struct.pack("<2f", *t) for t in uvs),
                        34962,
                        len(uvs),
                        "VEC2",
                        5126,
                    ),
                },
                "indices": add(
                    b"".join(struct.pack("<H", i) for i in indices),
                    34963,
                    len(indices),
                    "SCALAR",
                    5123,
                ),
                "material": names.index(material),
            }
        )

    gltf = {
        "asset": {"version": "2.0", "generator": "Oxygen surface test car"},
        "buffers": [
            {
                "uri": "data:application/octet-stream;base64,"
                + base64.b64encode(bytes(blob)).decode(),
                "byteLength": len(blob),
            }
        ],
        "bufferViews": views,
        "accessors": accessors,
        "materials": [
            {
                "name": name,
                "pbrMetallicRoughness": {
                    "baseColorFactor": color,
                    "metallicFactor": metal,
                    "roughnessFactor": rough,
                },
            }
            for name, (color, metal, rough) in MATERIALS.items()
        ],
        "meshes": [{"name": "SurfaceCar", "primitives": primitives}],
        "nodes": [{"name": "SurfaceCar", "mesh": 0}],
        "scenes": [{"name": "SurfaceCar", "nodes": [0]}],
        "scene": 0,
    }
    out.write_text(json.dumps(gltf, indent=2) + "\n", encoding="utf-8", newline="\n")


if __name__ == "__main__":
    main(pathlib.Path(sys.argv[1]))
