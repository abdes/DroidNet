#!/usr/bin/env python3
"""Generate the CityEnvironmentValidation scene and its descriptors.

A foggy-morning city in metres (X east, Y north, Z up): a river with two
bridges along the south, a street grid of kerbed blocks, a downtown core of
podium towers, midtown offices, residential perimeter blocks around
courtyards, a central park with a pond, a plaza, a tree-lined boulevard and a
riverside promenade. Generation is deterministic; rerun after editing:

    python generate_city.py

It rewrites the scene descriptor, the geometry and material descriptors and
the import manifest in this directory.
"""

from __future__ import annotations

import json
import math
import random
from dataclasses import dataclass
from pathlib import Path

HERE = Path(__file__).resolve().parent
SCENE_NAME = "CityEnvironmentValidation"
SLOT_ID = "146b1397-c3e0-bc20-fce3-c55e470b4086"
DEFAULT_MATERIAL = "MatCityConcreteWarm"
# Every geometry has one slot with this id and the same default material, so
# they share one material-slot layout revision.
LAYOUT_REVISION = "9aac2a2118c954ed5bbbd610708bef719ae518444cba5162086fcc2885a73713"

# --- Materials: muted, physically plausible albedos ------------------------

MATERIALS = {
    "MatCityAsphalt": dict(base_color=[0.05, 0.05, 0.055], roughness=0.9),
    "MatCityPavement": dict(base_color=[0.34, 0.33, 0.31], roughness=0.85),
    "MatCityConcreteWarm": dict(base_color=[0.48, 0.45, 0.40], roughness=0.75),
    "MatCityConcreteCool": dict(base_color=[0.40, 0.42, 0.44], roughness=0.7),
    "MatCityLimestone": dict(base_color=[0.62, 0.60, 0.55], roughness=0.65),
    "MatCityBrickRed": dict(base_color=[0.33, 0.15, 0.10], roughness=0.85),
    "MatCityBrickBuff": dict(base_color=[0.50, 0.40, 0.28], roughness=0.85),
    "MatCityGlassBlue": dict(base_color=[0.10, 0.13, 0.17], metalness=0.5, roughness=0.12),
    "MatCityGlassTeal": dict(base_color=[0.08, 0.12, 0.12], metalness=0.5, roughness=0.14),
    "MatCityGlassBronze": dict(base_color=[0.14, 0.11, 0.08], metalness=0.5, roughness=0.14),
    "MatCityMetalPanel": dict(base_color=[0.55, 0.56, 0.58], metalness=0.8, roughness=0.35),
    "MatCitySlateDark": dict(base_color=[0.10, 0.10, 0.11], roughness=0.6),
    "MatCityRoofGravel": dict(base_color=[0.20, 0.20, 0.19], roughness=0.95),
    "MatCityGrass": dict(base_color=[0.07, 0.13, 0.05], roughness=0.95),
    "MatCityTreeCanopy": dict(base_color=[0.05, 0.10, 0.035], roughness=0.9),
    "MatCityBark": dict(base_color=[0.09, 0.07, 0.05], roughness=0.9),
    "MatCityWater": dict(base_color=[0.02, 0.03, 0.035], roughness=0.06),
    "MatCityWoodTank": dict(base_color=[0.25, 0.18, 0.12], roughness=0.85),
}

# --- Unit procedural geometries (centred, unit extents) --------------------

GEOMETRIES = {
    "GeoCityCube": ("Cube", None, ([-0.5, -0.5, -0.5], [0.5, 0.5, 0.5])),
    "GeoCityCylinder": (
        "Cylinder", {"segments": 32, "height": 1.0, "radius": 0.5},
        ([-0.5, -0.5, -0.5], [0.5, 0.5, 0.5])),
    "GeoCityCone": (
        "Cone", {"segments": 24, "height": 1.0, "radius": 0.5},
        ([-0.5, -0.5, -0.5], [0.5, 0.5, 0.5])),
    "GeoCitySphere": (
        "IcoSphere", {"subdivision_level": 2},
        ([-0.5, -0.5, -0.5], [0.5, 0.5, 0.5])),
}

FLAGS_STATIC = {
    "visible": "shown",
    "static": True,
    "casts_shadows": "on",
    "receives_shadows": "on",
}


@dataclass
class Box:
    """Axis-aligned extents in metres."""

    x0: float
    y0: float
    x1: float
    y1: float

    @property
    def cx(self) -> float:
        return 0.5 * (self.x0 + self.x1)

    @property
    def cy(self) -> float:
        return 0.5 * (self.y0 + self.y1)

    @property
    def w(self) -> float:
        return self.x1 - self.x0

    @property
    def d(self) -> float:
        return self.y1 - self.y0

    def inset(self, m: float) -> "Box":
        return Box(self.x0 + m, self.y0 + m, self.x1 - m, self.y1 - m)


class SceneBuilder:
    def __init__(self) -> None:
        self.nodes: list[dict] = []
        self.renderables: list[dict] = []

    def node(self, name: str, translation, scale=(1, 1, 1), rotation=(0, 0, 0, 1),
             flags=None) -> int:
        entry = {
            "name": name,
            "flags": dict(flags or FLAGS_STATIC),
            "transform": {
                "translation": [round(v, 3) for v in translation],
                "rotation": [round(v, 8) for v in rotation],
                "scale": [round(v, 3) for v in scale],
            },
        }
        if self.nodes:
            entry["parent"] = 0
        self.nodes.append(entry)
        return len(self.nodes) - 1

    def mesh(self, name: str, geometry: str, material: str, translation, scale,
             rotation=(0, 0, 0, 1)) -> int:
        index = self.node(name, translation, scale, rotation)
        renderable = {
            "node": index,
            "geometry_ref": f"/.cooked/Geometry/{geometry}.ogeo",
            "visible": True,
        }
        if material != DEFAULT_MATERIAL:
            renderable["material_overrides"] = [{
                "slot_id": SLOT_ID,
                "material_ref": f"/.cooked/Materials/{material}.omat",
                "layout_revision": LAYOUT_REVISION,
            }]
        self.renderables.append(renderable)
        return index

    def box(self, name: str, material: str, b: Box, z0: float, z1: float) -> int:
        return self.mesh(name, "GeoCityCube", material,
                         (b.cx, b.cy, 0.5 * (z0 + z1)), (b.w, b.d, z1 - z0))

    def cylinder(self, name: str, material: str, x: float, y: float, radius: float,
                 z0: float, z1: float, geometry: str = "GeoCityCylinder") -> int:
        return self.mesh(name, geometry, material, (x, y, 0.5 * (z0 + z1)),
                         (2 * radius, 2 * radius, z1 - z0))

    def tree(self, name: str, x: float, y: float, rng: random.Random, base: float) -> None:
        height = rng.uniform(9.0, 15.0)
        canopy = rng.uniform(3.2, 4.8)
        self.cylinder(f"{name}_Trunk", "MatCityBark", x, y, 0.25, base,
                      base + 0.55 * height)
        self.mesh(f"{name}_Canopy", "GeoCitySphere", "MatCityTreeCanopy",
                  (x, y, base + height - canopy),
                  (2 * canopy, 2 * canopy, 2.2 * canopy))


# --- City layout ---------------------------------------------------------------

BLOCK_W, BLOCK_D = 150.0, 110.0
AVENUE, STREET = 30.0, 20.0
COLUMNS = range(-5, 6)
ROWS = range(0, 16)
GRID_Y0 = -75.0
KERB = 0.25
DOWNTOWN = (90.0, 820.0)
RIVER = Box(-4000.0, -345.0, 4000.0, -175.0)
WATER_Z = -3.0
PARK = {(-3, 7), (-2, 7), (-3, 8), (-2, 8)}
PLAZA = {(1, 3)}
BOULEVARD_X = 0.5 * (BLOCK_W + AVENUE)  # avenue east of column 0
BRIDGE_XS = (BOULEVARD_X, -3.5 * (BLOCK_W + AVENUE))


def block_box(i: int, j: int) -> Box:
    cx = i * (BLOCK_W + AVENUE)
    cy = GRID_Y0 + j * (BLOCK_D + STREET)
    return Box(cx - BLOCK_W / 2, cy - BLOCK_D / 2, cx + BLOCK_W / 2, cy + BLOCK_D / 2)


def downtown_distance(b: Box) -> float:
    return math.hypot(b.cx - DOWNTOWN[0], (b.cy - DOWNTOWN[1]) * 0.8)


def split_lots(b: Box, rng: random.Random, count: int) -> list[Box]:
    if count == 1:
        return [b]
    if count == 2:
        cut = b.x0 + b.w * rng.uniform(0.42, 0.58)
        return [Box(b.x0, b.y0, cut, b.y1), Box(cut, b.y0, b.x1, b.y1)]
    cut_x = b.x0 + b.w * rng.uniform(0.44, 0.56)
    cut_y = b.y0 + b.d * rng.uniform(0.44, 0.56)
    return [
        Box(b.x0, b.y0, cut_x, cut_y), Box(cut_x, b.y0, b.x1, cut_y),
        Box(b.x0, cut_y, cut_x, b.y1), Box(cut_x, cut_y, b.x1, b.y1),
    ]


def core_tower(s: SceneBuilder, name: str, lot: Box, rng: random.Random,
               height: float, landmark: str | None) -> None:
    podium_h = rng.uniform(10.0, 22.0)
    podium = lot.inset(3.0)
    s.box(f"{name}_Podium", rng.choice(["MatCityLimestone", "MatCityConcreteCool"]),
          podium, 0.0, KERB + podium_h)
    cladding = rng.choice(["MatCityGlassBlue", "MatCityGlassTeal", "MatCityGlassBronze",
                           "MatCityMetalPanel"])
    fx, fy = rng.uniform(0.45, 0.62), rng.uniform(0.5, 0.7)
    tw, td = podium.w * fx, podium.d * fy
    tower = Box(podium.cx - tw / 2, podium.cy - td / 2, podium.cx + tw / 2, podium.cy + td / 2)
    base = KERB + podium_h
    if landmark == "round":
        radius = 0.5 * min(tw, td)
        s.cylinder(f"{name}_Tower", cladding, tower.cx, tower.cy, radius, base - 1, height)
        s.cylinder(f"{name}_Crown", "MatCityMetalPanel", tower.cx, tower.cy, radius * 0.7,
                   height - 1, height + 12)
        return
    setback = height * rng.uniform(0.72, 0.85)
    s.box(f"{name}_Tower", cladding, tower, base - 1, setback)
    upper = tower.inset(min(tw, td) * 0.12)
    s.box(f"{name}_Upper", cladding, upper, setback - 1, height)
    s.box(f"{name}_Plant", "MatCitySlateDark", upper.inset(min(upper.w, upper.d) * 0.2),
          height - 1, height + 6)
    if landmark == "spire":
        s.cylinder(f"{name}_Spire", "MatCityMetalPanel", upper.cx, upper.cy, 4.0,
                   height + 5, height + 70, geometry="GeoCityCone")


def office(s: SceneBuilder, name: str, lot: Box, rng: random.Random, height: float) -> None:
    body = lot.inset(rng.uniform(2.0, 4.0))
    material = rng.choice(["MatCityConcreteWarm", "MatCityConcreteCool", "MatCityLimestone",
                           "MatCityGlassBlue", "MatCityBrickBuff", "MatCityGlassBronze"])
    if height > 55.0 and rng.random() < 0.4:
        step = height * rng.uniform(0.6, 0.75)
        s.box(f"{name}_Base", material, body, 0.0, step)
        s.box(f"{name}_Top", material, body.inset(min(body.w, body.d) * 0.15), step - 1, height)
        top = body.inset(min(body.w, body.d) * 0.15)
    else:
        s.box(f"{name}_Body", material, body, 0.0, height)
        top = body
    plant = top.inset(min(top.w, top.d) * rng.uniform(0.25, 0.35))
    s.box(f"{name}_Plant", "MatCityRoofGravel", plant, height - 0.5, height + rng.uniform(3.0, 5.0))


def residential_block(s: SceneBuilder, name: str, b: Box, rng: random.Random) -> None:
    depth = 16.0
    inner = b.inset(2.0)
    bars = [
        Box(inner.x0, inner.y0, inner.x1, inner.y0 + depth),
        Box(inner.x0, inner.y1 - depth, inner.x1, inner.y1),
        Box(inner.x0, inner.y0 + depth, inner.x0 + depth, inner.y1 - depth),
        Box(inner.x1 - depth, inner.y0 + depth, inner.x1, inner.y1 - depth),
    ]
    for k, bar in enumerate(bars):
        # Long sides read as two buildings of different heights and facades.
        parts = [bar]
        if bar.w > 80.0:
            cut = bar.x0 + bar.w * rng.uniform(0.4, 0.6)
            parts = [Box(bar.x0, bar.y0, cut, bar.y1), Box(cut, bar.y0, bar.x1, bar.y1)]
        for m, part in enumerate(parts):
            height = rng.choice([15.0, 18.0, 21.0, 24.0, 27.0])
            material = rng.choice(["MatCityBrickRed", "MatCityBrickBuff", "MatCityConcreteWarm",
                                   "MatCityLimestone"])
            s.box(f"{name}_Bar{k}{m}", material, part, 0.0, height)
            if rng.random() < 0.12:
                x = part.cx + rng.uniform(-0.25, 0.25) * part.w
                s.cylinder(f"{name}_Tank{k}{m}", "MatCityWoodTank", x, part.cy, 2.2,
                           height, height + 5.0)
                s.cylinder(f"{name}_TankRoof{k}{m}", "MatCitySlateDark", x, part.cy, 2.4,
                           height + 5.0, height + 6.5, geometry="GeoCityCone")
    court = b.inset(2.0 + depth)
    s.box(f"{name}_Courtyard", "MatCityGrass", court, KERB - 0.1, KERB + 0.02)
    s.tree(f"{name}_CourtTree", court.cx + rng.uniform(-8, 8), court.cy + rng.uniform(-5, 5),
           rng, KERB)


def build() -> SceneBuilder:
    rng = random.Random(20261007)
    s = SceneBuilder()
    s.node("Root", (0, 0, 0))

    # Ground: open land, the asphalt street network and the river channel.
    s.box("Land_9000m", "MatCityGrass", Box(-4500, -3000, 4500, 6000), -2.0, -0.05)
    city = Box(block_box(COLUMNS[0], 0).x0 - AVENUE, RIVER.y1,
               block_box(COLUMNS[-1], 0).x1 + AVENUE, block_box(0, ROWS[-1]).y1 + STREET)
    s.box("Streets_Asphalt", "MatCityAsphalt", city, -1.0, 0.0)
    s.box("River_Water", "MatCityWater", RIVER, WATER_Z - 4.0, WATER_Z)
    s.box("River_NorthQuay", "MatCityConcreteCool",
          Box(RIVER.x0, RIVER.y1 - 2.0, RIVER.x1, RIVER.y1), WATER_Z - 4.0, 0.3)
    s.box("River_SouthQuay", "MatCityConcreteCool",
          Box(RIVER.x0, RIVER.y0, RIVER.x1, RIVER.y0 + 2.0), WATER_Z - 4.0, 0.3)
    south_bank = Box(-2400, -700, 2400, RIVER.y0)
    s.box("SouthBank_Promenade", "MatCityPavement", south_bank, -1.0, KERB)
    for x in BRIDGE_XS:
        deck = Box(x - 14.0, RIVER.y0 - 6.0, x + 14.0, RIVER.y1 + 6.0)
        s.box(f"Bridge_{int(x)}_Deck", "MatCityConcreteCool", deck, 0.0, 1.6)
        for side in (-1, 1):
            s.box(f"Bridge_{int(x)}_Parapet{side}", "MatCityLimestone",
                  Box(deck.x0 if side < 0 else deck.x1 - 0.6, deck.y0,
                      deck.x0 + 0.6 if side < 0 else deck.x1, deck.y1), 1.6, 2.7)
        for y in (RIVER.cy - 40.0, RIVER.cy + 40.0):
            s.box(f"Bridge_{int(x)}_Pier{int(y)}", "MatCityConcreteCool",
                  Box(x - 10.0, y - 3.0, x + 10.0, y + 3.0), WATER_Z - 4.0, 0.2)
    for k, x in enumerate(range(-1500, 1501, 30)):
        if any(abs(x - bx) < 20 for bx in BRIDGE_XS):
            continue
        s.tree(f"Promenade_Tree{k}", x + rng.uniform(-3, 3), RIVER.y0 - 14.0, rng, KERB)

    # Blocks: kerbed pavement plus the district's buildings.
    landmarks = {(0, 6): "spire", (1, 7): "round", (-1, 8): "plain"}
    for i in COLUMNS:
        for j in ROWS:
            b = block_box(i, j)
            tag = f"B{i:+d}_{j:02d}"
            if (i, j) in PARK:
                s.box(f"{tag}_ParkLawn", "MatCityGrass", b, -1.0, KERB)
                continue
            s.box(f"{tag}_Pavement", "MatCityPavement", b, -1.0, KERB)
            if (i, j) in PLAZA:
                for t in range(6):
                    s.tree(f"{tag}_PlazaTree{t}", b.x0 + 20 + t * 22, b.cy + rng.uniform(-30, 30),
                           rng, KERB)
                continue
            distance = downtown_distance(b)
            if distance < 420.0:
                lots = split_lots(b.inset(3.0), rng, rng.choice([1, 2]))
                for n, lot in enumerate(lots):
                    landmark = landmarks.get((i, j)) if n == 0 else None
                    if landmark:
                        height = {"spire": 300.0, "round": 250.0, "plain": 210.0}[landmark]
                    else:
                        height = rng.uniform(95.0, 170.0) * (1.0 - distance / 900.0)
                        height = max(height, 70.0)
                    core_tower(s, f"{tag}_L{n}", lot, rng, height, landmark)
            elif distance < 950.0:
                lots = split_lots(b.inset(3.0), rng, rng.choice([2, 2, 4]))
                for n, lot in enumerate(lots):
                    scale = 1.0 - (distance - 420.0) / 900.0
                    office(s, f"{tag}_L{n}", lot, rng, rng.uniform(32.0, 88.0) * scale + 12.0)
            else:
                residential_block(s, tag, b, rng)

    # Park: pond, paths and trees between the lawn blocks.
    park_blocks = [block_box(i, j) for i, j in PARK]
    park = Box(min(p.x0 for p in park_blocks), min(p.y0 for p in park_blocks),
               max(p.x1 for p in park_blocks), max(p.y1 for p in park_blocks))
    s.box("Park_LawnJoin", "MatCityGrass", park, -1.0, KERB - 0.05)
    pond = Box(park.cx - 70, park.cy - 35, park.cx + 50, park.cy + 30)
    s.box("Park_Pond", "MatCityWater", pond, -1.0, KERB + 0.02)
    for t in range(70):
        while True:
            x, y = rng.uniform(park.x0 + 8, park.x1 - 8), rng.uniform(park.y0 + 8, park.y1 - 8)
            if not (pond.x0 - 6 < x < pond.x1 + 6 and pond.y0 - 6 < y < pond.y1 + 6):
                break
        s.tree(f"Park_Tree{t}", x, y, rng, KERB)

    # Boulevard: tree rows on both kerbs of the avenue east of column 0.
    for k, y in enumerate(range(int(RIVER.y1) + 30, int(block_box(0, 9).y1), 26)):
        for side in (-1, 1):
            s.tree(f"Boulevard_Tree{k}{'W' if side < 0 else 'E'}",
                   BOULEVARD_X + side * (AVENUE / 2 - 2.5), y + rng.uniform(-2, 2), rng, 0.0)
    return s


# --- Environment: a foggy morning ------------------------------------------------


def look_rotation(forward, up=(0.0, 0.0, 1.0)):
    """Quaternion (x, y, z, w) turning view -Z to `forward` with +Y up."""
    f = _normalize(forward)
    r = _normalize(_cross(f, up))
    u = _cross(r, f)
    # Columns: right, up, -forward.
    m = [[r[0], u[0], -f[0]], [r[1], u[1], -f[1]], [r[2], u[2], -f[2]]]
    return _matrix_to_quaternion(m)


def light_rotation(to_source):
    """Quaternion whose emitted ray (local -Y) travels away from `to_source`."""
    travel = _normalize([-c for c in to_source])
    # Rotate local -Y onto `travel`.
    return _rotation_between((0.0, -1.0, 0.0), travel)


def _normalize(v):
    n = math.sqrt(sum(c * c for c in v))
    return [c / n for c in v]


def _cross(a, b):
    return [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]]


def _matrix_to_quaternion(m):
    trace = m[0][0] + m[1][1] + m[2][2]
    if trace > 0:
        s = math.sqrt(trace + 1.0) * 2
        return [(m[2][1] - m[1][2]) / s, (m[0][2] - m[2][0]) / s, (m[1][0] - m[0][1]) / s, 0.25 * s]
    if m[0][0] > m[1][1] and m[0][0] > m[2][2]:
        s = math.sqrt(1.0 + m[0][0] - m[1][1] - m[2][2]) * 2
        return [0.25 * s, (m[0][1] + m[1][0]) / s, (m[0][2] + m[2][0]) / s, (m[2][1] - m[1][2]) / s]
    if m[1][1] > m[2][2]:
        s = math.sqrt(1.0 + m[1][1] - m[0][0] - m[2][2]) * 2
        return [(m[0][1] + m[1][0]) / s, 0.25 * s, (m[1][2] + m[2][1]) / s, (m[0][2] - m[2][0]) / s]
    s = math.sqrt(1.0 + m[2][2] - m[0][0] - m[1][1]) * 2
    return [(m[0][2] + m[2][0]) / s, (m[1][2] + m[2][1]) / s, 0.25 * s, (m[1][0] - m[0][1]) / s]


def _rotation_between(a, b):
    a, b = _normalize(a), _normalize(b)
    axis = _cross(a, b)
    w = 1.0 + sum(x * y for x, y in zip(a, b))
    q = [axis[0], axis[1], axis[2], w]
    return _normalize(q)


SUN_ELEVATION_DEG = 12.0
SUN_AZIMUTH_DEG = 55.0  # counter-clockwise from east: north-east
CAMERA_POSITION = (-60.0, -470.0, 30.0)
CAMERA_TARGET = (180.0, 900.0, 70.0)


def environment() -> dict:
    return {
        "sky_atmosphere": {
            "enabled": True,
            "planet_radius_m": 6360000.0,
            "atmosphere_height_m": 100000.0,
            "ground_albedo_rgb": [0.16, 0.15, 0.14],
            "rayleigh_scattering_rgb": [5.802e-06, 1.3558e-05, 3.31e-05],
            "rayleigh_scale_height_m": 8000.0,
            "mie_scattering_rgb": [3.996e-06, 3.996e-06, 3.996e-06],
            "mie_absorption_rgb": [4.405e-07, 4.405e-07, 4.405e-07],
            "mie_scale_height_m": 1200.0,
            "mie_anisotropy": 0.8,
            "ozone_absorption_rgb": [6.5e-07, 1.881e-06, 8.5e-08],
            "ozone_density_profile": [10000.0, 25000.0, 40000.0],
            "multi_scattering_factor": 1.0,
            "sky_luminance_factor_rgb": [1.0, 1.0, 1.0],
            "sky_and_aerial_perspective_luminance_factor_rgb": [1.0, 1.0, 1.0],
            "aerial_perspective_distance_scale": 1.0,
            "aerial_scattering_strength": 1.0,
            "aerial_perspective_start_depth_m": 40.0,
            "height_fog_contribution": 1.0,
            "trace_sample_count_scale": 1.0,
            "transmittance_min_light_elevation_deg": -90.0,
            "sun_disk_enabled": True,
            "holdout": False,
            "render_in_main_pass": True,
        },
        # Ground fog with ~1.2 km street-level visibility that thins above
        # ~150 m, over a thin haze layer. Height fog transmittance is
        # exp2(-ln2 * density * length), and volumetric fog matches it at half
        # the density, so the effective extinction is about 0.5 * density:
        # 0.0065 gives exp(-0.00325 * 1200) ~ 2%. Thick fog scatters many
        # times, which evens out light and shadow: a moderate phase, a weaker
        # sun share and a stronger sky share stand in for that.
        "fog": {
            "enabled": True,
            # Analytic height fog only: Vortex volumetric fog still shows froxel
            # slice banding and jitter shimmer without temporal anti-aliasing.
            "model": 0,
            "extinction_sigma_t_per_m": 0.0065,
            "height_falloff_per_m": 0.01,
            "height_offset_m": 0.0,
            "start_distance_m": 0.0,
            "max_opacity": 1.0,
            "single_scattering_albedo_rgb": [0.97, 0.97, 0.97],
            "anisotropy_g": 0.3,
            "enable_height_fog": True,
            "enable_volumetric_fog": False,
            "second_fog_density": 0.0006,
            "second_fog_height_falloff": 0.0015,
            "second_fog_height_offset": 0.0,
            "fog_inscattering_luminance": [0.0, 0.0, 0.0],
            "sky_atmosphere_ambient_contribution_color_scale": [1.0, 1.0, 1.0],
            "inscattering_color_cubemap_angle": 0.0,
            "inscattering_texture_tint": [1.0, 1.0, 1.0],
            "fully_directional_inscattering_color_distance": 1800.0,
            "non_directional_inscattering_color_distance": 600.0,
            "directional_inscattering_luminance": [1.0, 0.86, 0.68],
            "directional_inscattering_exponent": 12.0,
            "directional_inscattering_start_distance": 0.0,
            "end_distance_m": 0.0,
            "fog_cutoff_distance_m": 0.0,
            "volumetric_fog_scattering_distribution": 0.3,
            "volumetric_fog_albedo": [0.95, 0.95, 0.95],
            "volumetric_fog_emissive": [0.0, 0.0, 0.0],
            "volumetric_fog_extinction_scale": 1.0,
            "volumetric_fog_distance": 1200.0,
            "volumetric_fog_start_distance": 0.0,
            "volumetric_fog_near_fade_in_distance": 5.0,
            "volumetric_fog_static_lighting_scattering_intensity": 0.4,
            "override_light_colors_with_fog_inscattering_colors": False,
            "holdout": False,
            "render_in_main_pass": True,
            "visible_in_reflection_captures": True,
            "visible_in_real_time_sky_captures": True,
        },
        "sky_light": {
            "enabled": True,
            "source": 0,
            "intensity": 1.0,
            "tint_rgb": [0.95, 0.96, 1.0],
            "diffuse_intensity": 1.0,
            "specular_intensity": 0.5,
            "lower_hemisphere_color": [0.03, 0.03, 0.03],
            "volumetric_scattering_intensity": 1.5,
            "affect_reflections": True,
        },
    }


def scene_document() -> dict:
    s = build()
    camera = s.node("MainCamera", CAMERA_POSITION,
                    rotation=look_rotation([t - p for t, p in zip(CAMERA_TARGET, CAMERA_POSITION)]),
                    flags={"visible": "shown", "casts_shadows": "on", "receives_shadows": "on"})
    el, az = math.radians(SUN_ELEVATION_DEG), math.radians(SUN_AZIMUTH_DEG)
    to_sun = [math.cos(el) * math.cos(az), math.cos(el) * math.sin(az), math.sin(el)]
    sun = s.node("SunFoggyMorning", [3000.0 * c for c in to_sun],
                 rotation=light_rotation(to_sun),
                 flags={"visible": "shown", "casts_shadows": "on", "receives_shadows": "on"})
    mist = s.node("RiverMist_LocalFog", (0.0, RIVER.cy, 12.0), scale=(5000.0, 260.0, 40.0),
                  flags={"visible": "shown", "casts_shadows": "on", "receives_shadows": "on"})
    return {
        "version": 11,
        "name": SCENE_NAME,
        "nodes": s.nodes,
        "renderables": s.renderables,
        "cameras": {"perspective": [{
            "node": camera,
            "fov_y": math.radians(55.0),
            "aspect_mode": "fixed",
            "aspect_ratio": 16.0 / 9.0,
            "near_plane": 0.2,
            "far_plane": 8000.0,
        }]},
        "lights": {"directional": [{
            "node": sun,
            "common": {
                "affects_world": True,
                "color_rgb": [1.0, 0.9, 0.78],
                "casts_shadows": True,
                "shadow": {"bias": 0.125, "normal_bias": 0.02, "contact_shadows": True,
                           "resolution_hint": 1},
            },
            "angular_size_radians": 0.00951,
            "atmosphere_light_slot": 1,
            "cascade_count": 4,
            "split_mode": 1,
            "max_shadow_distance": 2400.0,
            "cascade_distances": [120.0, 400.0, 1100.0, 2400.0],
            "distribution_exponent": 2.0,
            "transition_fraction": 0.12,
            "distance_fadeout_fraction": 0.1,
            "intensity_lux": 25000.0,
        }]},
        "environment": environment(),
        "local_fog_volumes": [{
            "node": mist,
            "enabled": True,
            "radial_fog_extinction": 0.0,
            "height_fog_extinction": 0.02,
            "height_fog_falloff": 0.08,
            "height_fog_offset": -3.0,
            "fog_phase_g": 0.5,
            "fog_albedo": [0.95, 0.95, 0.95],
            "fog_emissive": [0.0, 0.0, 0.0],
            "sort_priority": 1,
        }],
        "references": {
            "materials": [f"/.cooked/Materials/{m}.omat" for m in MATERIALS],
        },
    }


def material_document(name: str, params: dict) -> dict:
    return {
        "name": name,
        "domain": "opaque",
        "alpha_mode": "opaque",
        "parameters": {
            "base_color": params["base_color"] + [1.0],
            "metalness": params.get("metalness", 0.0),
            "roughness": params["roughness"],
            "ambient_occlusion": 1.0,
        },
    }


def geometry_document(name: str, generator: str, params: dict | None, bounds) -> dict:
    procedural = {"generator": generator, "mesh_name": name}
    if params:
        procedural["params"] = params
    lo, hi = bounds
    return {
        "name": name,
        "bounds": {"min": lo, "max": hi},
        "lods": [{
            "name": f"{name}_lod0",
            "mesh_type": "procedural",
            "bounds": {"min": lo, "max": hi},
            "procedural": procedural,
            "submeshes": [{
                "name": f"{name}_surface",
                "slot_id": SLOT_ID,
                "material_ref": f"/.cooked/Materials/{DEFAULT_MATERIAL}.omat",
                "views": [{"view_ref": "__all__"}],
            }],
        }],
    }


def manifest_document() -> dict:
    jobs = []
    for name in MATERIALS:
        jobs.append({"id": f"material.{name}", "type": "material-descriptor",
                     "source": f"./{name}.material.json", "name": name,
                     "content_hashing": True})
    for name in GEOMETRIES:
        jobs.append({"id": f"geometry.{name}", "type": "geometry-descriptor",
                     "source": f"./{name}.geometry.json", "name": name,
                     "content_hashing": True,
                     "depends_on": [f"material.{DEFAULT_MATERIAL}"]})
    jobs.append({"id": f"scene.{SCENE_NAME}", "type": "scene-descriptor",
                 "source": f"./{SCENE_NAME}.scene.json", "name": SCENE_NAME,
                 "content_hashing": True,
                 "depends_on": [f"geometry.{g}" for g in GEOMETRIES]
                 + [f"material.{m}" for m in MATERIALS]})
    return {"version": 1, "max_in_flight_jobs": 1, "output": "../../.cooked/main", "jobs": jobs}


def write_json(path: Path, document: dict) -> None:
    path.write_text(json.dumps(document, indent=2) + "\n", encoding="utf-8", newline="\n")


def main() -> None:
    for stale in HERE.glob("MatCity*.material.json"):
        if stale.name.removesuffix(".material.json") not in MATERIALS:
            stale.unlink()
    for name, params in MATERIALS.items():
        write_json(HERE / f"{name}.material.json", material_document(name, params))
    for name, (generator, params, bounds) in GEOMETRIES.items():
        write_json(HERE / f"{name}.geometry.json",
                   geometry_document(name, generator, params, bounds))
    scene = scene_document()
    write_json(HERE / f"{SCENE_NAME}.scene.json", scene)
    write_json(HERE / "import-manifest.json", manifest_document())
    print(f"{len(scene['nodes'])} nodes, {len(scene['renderables'])} renderables")


if __name__ == "__main__":
    main()
