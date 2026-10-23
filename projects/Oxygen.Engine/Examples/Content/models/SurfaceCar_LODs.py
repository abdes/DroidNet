"""Blender script: writes SurfaceCar_LODs.blend and SurfaceCar_LODs.fbx.

A toy car with three LODs in an FBX LOD group. Run it from this directory:

    blender --background --factory-startup --python SurfaceCar_LODs.py -- .

The car uses five materials (Paint, Glass, Interior, Rubber, Chrome) in every LOD,
so each surface binds a submesh in each LOD. The LODs differ visibly:

- LOD0: rounded edges, 32-sided wheels, separate seats and side mirrors;
- LOD1: chamfered edges, 12-sided wheels and two seat blocks;
- LOD2: sharp boxes, hexagonal wheels and one seat block.

The root is an Empty whose 'fbx_type' custom property makes Blender's exporter write
it as an FBX LodGroup. Its children are named SurfaceCar_LOD0..2, so the name
convention also holds. Blender is Z up with X forward here; the exporter converts to
FBX's Y up.
"""

import sys
from pathlib import Path

import bmesh
import bpy
from mathutils import Matrix, Vector

MATERIALS = {
    "Paint": ((0.75, 0.05, 0.05, 1.0), 0.3, 0.35),
    "Glass": ((0.55, 0.75, 0.9, 1.0), 0.0, 0.05),
    "Interior": ((0.35, 0.2, 0.1, 1.0), 0.0, 0.8),
    "Rubber": ((0.03, 0.03, 0.03, 1.0), 0.0, 0.9),
    "Chrome": ((0.9, 0.9, 0.9, 1.0), 1.0, 0.15),
}
SLOTS = list(MATERIALS)

WHEELS = [(x, y) for x in (-0.65, 0.65) for y in (-0.55, 0.55)]


def lod_spec(lod):
    """What differs per LOD: bevels, wheel sides, seat blocks and mirrors."""
    return {
        0: {
            "bevel": (0.06, 4),
            "glass_bevel": (0.05, 3),
            "trim_bevel": (0.02, 2),
            "wheel_sides": 32,
            "seats": "separate",
            "mirrors": True,
        },
        1: {
            "bevel": (0.06, 1),
            "glass_bevel": (0.04, 1),
            "trim_bevel": None,
            "wheel_sides": 12,
            "seats": "blocks",
            "mirrors": False,
        },
        2: {
            "bevel": None,
            "glass_bevel": None,
            "trim_bevel": None,
            "wheel_sides": 6,
            "seats": "one",
            "mirrors": False,
        },
    }[lod]


def make_material(name, color, metallic, roughness):
    material = bpy.data.materials.new(name)
    material.diffuse_color = color
    material.metallic = metallic
    material.roughness = roughness
    if material.node_tree is None:
        material.use_nodes = True
    shader = material.node_tree.nodes.get("Principled BSDF")
    if shader is not None:
        shader.inputs["Base Color"].default_value = color
        shader.inputs["Metallic"].default_value = metallic
        shader.inputs["Roughness"].default_value = roughness
    return material


def box_matrix(lo, hi):
    lo, hi = Vector(lo), Vector(hi)
    size = hi - lo
    return Matrix.Translation((lo + hi) / 2) @ Matrix.Diagonal(
        (size.x, size.y, size.z, 1.0)
    )


def add_part(bm, verts, material, bevel=None):
    """Assigns the part's faces their material, then rounds its edges.

    Bevel faces take the material of the faces they join.
    """
    faces = {face for vert in verts for face in vert.link_faces}
    for face in faces:
        face.material_index = SLOTS.index(material)
    if bevel:
        width, segments = bevel
        edges = list({edge for vert in verts for edge in vert.link_edges})
        bmesh.ops.bevel(
            bm,
            geom=list(verts) + edges,
            offset=width,
            segments=segments,
            profile=0.5,
            affect="EDGES",
            clamp_overlap=True,
        )


def add_box(bm, lo, hi, material, bevel=None):
    verts = bmesh.ops.create_cube(bm, size=1.0, matrix=box_matrix(lo, hi))["verts"]
    add_part(bm, verts, material, bevel)


def add_wheel(bm, x, y, sides):
    # A cylinder along Y: create_cone builds along Z, so turn it a quarter about X.
    matrix = Matrix.Translation((x, y, 0.2)) @ Matrix.Rotation(1.5707963, 4, "X")
    verts = bmesh.ops.create_cone(
        bm,
        cap_ends=True,
        cap_tris=False,
        segments=sides,
        radius1=0.2,
        radius2=0.2,
        depth=0.16,
        matrix=matrix,
    )["verts"]
    add_part(bm, verts, "Rubber")


def add_seats(bm, style):
    if style == "separate":
        for x in (0.15, -0.25):
            for y in (-0.18, 0.18) if x > 0 else (0.0,):
                width = 0.14 if x > 0 else 0.32
                add_box(
                    bm,
                    (x - 0.12, y - width, 0.7),
                    (x + 0.12, y + width, 0.8),
                    "Interior",
                    (0.02, 2),
                )
                add_box(
                    bm,
                    (x - 0.14, y - width, 0.8),
                    (x - 0.08, y + width, 1.02),
                    "Interior",
                    (0.02, 2),
                )
    elif style == "blocks":
        add_box(bm, (0.0, -0.33, 0.7), (0.28, 0.33, 0.95), "Interior")
        add_box(bm, (-0.4, -0.33, 0.7), (-0.1, 0.33, 0.95), "Interior")
    else:
        add_box(bm, (-0.4, -0.33, 0.7), (0.28, 0.33, 0.92), "Interior")


def uv_project(bm):
    """Gives every LOD texture coordinates by box projection.

    Each face takes the two axes across its normal.
    """
    layer = bm.loops.layers.uv.new("UVMap")
    for face in bm.faces:
        axis = max(range(3), key=lambda i: abs(face.normal[i]))
        u_axis, v_axis = [i for i in range(3) if i != axis]
        for loop in face.loops:
            loop[layer].uv = (loop.vert.co[u_axis], loop.vert.co[v_axis])


def build_lod(lod, materials, parent):
    spec = lod_spec(lod)
    bm = bmesh.new()
    add_box(bm, (-1.0, -0.5, 0.25), (1.0, 0.5, 0.7), "Paint", spec["bevel"])
    add_box(bm, (-0.45, -0.45, 0.7), (0.55, 0.45, 1.1), "Glass", spec["glass_bevel"])
    add_seats(bm, spec["seats"])
    for x, y in WHEELS:
        add_wheel(bm, x, y, spec["wheel_sides"])
    add_box(bm, (1.0, -0.45, 0.25), (1.08, 0.45, 0.4), "Chrome", spec["trim_bevel"])
    add_box(bm, (-1.08, -0.45, 0.25), (-1.0, 0.45, 0.4), "Chrome", spec["trim_bevel"])
    if spec["mirrors"]:
        for side in (-1, 1):
            add_box(
                bm,
                (0.45, side * 0.5 - 0.06, 0.78),
                (0.53, side * 0.5 + 0.06, 0.86),
                "Chrome",
                (0.01, 2),
            )
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    uv_project(bm)

    mesh = bpy.data.meshes.new(f"SurfaceCar_LOD{lod}")
    bm.to_mesh(mesh)
    bm.free()
    for material in materials:
        mesh.materials.append(material)
    obj = bpy.data.objects.new(f"SurfaceCar_LOD{lod}", mesh)
    bpy.context.scene.collection.objects.link(obj)
    obj.parent = parent
    return obj, len(mesh.polygons)


def main(directory):
    bpy.ops.wm.read_factory_settings(use_empty=True)
    materials = [make_material(name, *values) for name, values in MATERIALS.items()]

    group = bpy.data.objects.new("SurfaceCar", None)
    group["fbx_type"] = "LodGroup"
    bpy.context.scene.collection.objects.link(group)

    for lod in range(3):
        obj, polygons = build_lod(lod, materials, group)
        print(f"{obj.name}: {polygons} polygons")

    folder = Path(directory).resolve()
    # Overwrite the .blend without leaving a .blend1 backup beside it.
    bpy.context.preferences.filepaths.save_version = 0
    bpy.ops.wm.save_as_mainfile(
        filepath=str(folder / "SurfaceCar_LODs.blend"), compress=True
    )
    output = str(folder / "SurfaceCar_LODs.fbx")
    bpy.ops.export_scene.fbx(
        filepath=output,
        use_selection=False,
        object_types={"EMPTY", "MESH"},
        mesh_smooth_type="FACE",
        add_leaf_bones=False,
        bake_anim=False,
        path_mode="AUTO",
    )
    print(f"Wrote {output}")


if __name__ == "__main__":
    main(sys.argv[sys.argv.index("--") + 1])
