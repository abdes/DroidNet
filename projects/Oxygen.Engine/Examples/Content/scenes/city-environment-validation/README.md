# City Environment Validation Scene

`CityEnvironmentValidation.scene.json` is a meter-authored city-scale scene for validating Oxygen environment rendering. The default unit is meters: the ground slab is 7000 m x 4200 m, the far skyline extends beyond 3 km from the default camera, 309 building cubes use footprints of tens of meters, and landmark towers reach 210 m, 260 m, and 320 m.

Scene-authored environment coverage:

- `environment.sky_atmosphere` enables Earth-scale sky, sun disk, aerial perspective, and height-fog contribution.
- `environment.fog` enables height fog plus volumetric fog parameters with kilometer-scale start/end distances.
- `environment.sky_light` enables captured-scene sky lighting with diffuse/specular and volumetric-scattering controls.
- `local_fog_volumes` adds three authored local fog volumes at near, mid, and far city distances.
- The directional sun is a scene light marked as both `environment_contribution` and `is_sun_light`, with four meter-scale shadow cascades.

The city uses one procedural cube geometry with scene-authored `renderables[].material_ref` overrides, so material variety validates the runtime scene material-override path directly.
