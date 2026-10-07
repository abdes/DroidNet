# City Environment Validation Scene

`CityEnvironmentValidation.scene.json` is a foggy-morning city in metres
(X east, Y north, Z up) for validating Oxygen environment rendering at city
scale: atmosphere, height fog, sky light and directional shadows over a dense,
readable urban layout.

- A river with quays and two bridges runs along the south; the default camera
  stands on the south-bank promenade looking north at the skyline.
- North of the river, 150 m x 110 m kerbed blocks sit on a grid of 30 m avenues
  and 20 m streets. Districts follow distance from downtown: podium towers with
  setbacks and three landmarks (spire, round and plain, 210-300 m), midtown
  offices with rooftop plant, and residential perimeter blocks around planted
  courtyards with occasional rooftop water tanks.
- A central park with a pond, a plaza, a tree-lined boulevard and riverside
  trees provide open space.
- Materials use muted, physically plausible albedos.
- The environment is a low north-east sun through ground fog with about 1.2 km
  street-level visibility over a thin haze layer, plus a local mist volume
  over the river. The fog is analytic height fog; volumetric fog is disabled
  until it is stable without temporal anti-aliasing.

The scene uses unit procedural geometries (cube, cylinder, cone and
icosphere) that share one material-slot layout, so every renderable selects
its material through a scene override.

## Regenerate

`generate_city.py` produces the scene descriptor, the geometry and material
descriptors and the import manifest deterministically. Edit it, never the
generated JSON, then run from this directory:

```powershell
python generate_city.py
```

## Run and inspect

From the Content directory, cook the scene using the [content workflow](../../README.md):

```powershell
./cook_scenes.cmd -Scene city-environment-validation -NoTUI
```

Select `CityEnvironmentValidation` in RenderScene Library and choose **Use Scene**
to restore its authored environment. Use the authored camera for repeatable
comparisons; see the [RenderScene guide](../../../RenderScene/README.md) for
camera reset, exposure and verification controls.
