# Model LODs

Shows the LOD groups a model import makes, under a sky and a sun.

| Where                     | What                                                                                                                               |
| ------------------------- | ---------------------------------------------------------------------------------------------------------------------------------- |
| Left lane, blue           | `LodStacked.gltf`: a node's `MSFT_lod` extension lists its coarser levels                                                          |
| Right lane, violet        | `LodNamedSiblings.gltf`: the same levels, no extension, siblings named `Sphere_LOD0` to `_LOD2`                                    |
| Riding each lane          | An instance on the imported policy, moving away from the camera and back                                                           |
| Beside each lane          | Three instances fixed to LOD 1, 2 and 3, nearest first                                                                             |
| Behind those              | The model's own scene, in the file's materials                                                                                     |
| Far left and far right    | `MSFT_lod_test.gltf`, the real file from the Babylon.js assets, unmodified: its own scene, and its geometry on the imported policy |
| Raised, in the far middle | `MSFT_lod_complex_hierarchy.gltf`, whose LOD group is refused                                                                      |

`LodStacked.gltf` is the real file with each sphere moved directly above the box,
so both stay in view; the two lanes differ only in how the file states its LODs.

The script colours an instance by the LOD it draws: **green** LOD 1, **yellow** LOD
2, **red** LOD 3. The files bind the same two materials on every LOD and their normals
are smooth, so without the colours a shaded view hides the change; the wireframe
view mode shows the triangles themselves.

In its first frames the script checks every instance through the renderable API and
logs one `[model-lods] PASS` or `FAIL` line per check, ending with a count: three
LODs that bind the file's two materials, each fixed policy drawing its LOD, and the
imported policy following the distance. The ride starts after that.

The levels of `MSFT_lod_complex_hierarchy.gltf` are whole subtrees, which a geometry
LOD cannot hold, so the import reports the group (`import.lod_group.ignored`) and
shows LOD0 and its children. Its other levels are outside the file's scene, so their
meshes cook but no node shows them.

Cooking this example warns of `MSFT_screencoverage`, which is not imported: both
files' LODs switch by the default screen-space error.

From the examples project root (see the [content workflow](../../../README.md)):

```powershell
Examples/RenderScene/Tools/cook ModelLods
Examples/RenderScene/Tools/pak
Examples/RenderScene/RenderScene.ps1 --scene ModelLods
```
