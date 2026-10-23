# Surface overrides

Shows the per-instance overrides of a geometry's asset defaults: surface
material, Visibility and Cast Shadows, and the LOD policy. Each row puts an
instance with the asset defaults next to instances that override them, in the
scene or from the script. The `Showcase` script applies the runtime overrides,
checks every instance through the renderable API and logs one
`[surface-overrides] PASS` or `FAIL` line per check, ending with a count. The
scene-authored instances prove the cooked overrides reach the renderables.

The scene uses the lamp and potted-plant models that `sdk-furniture` cooks, so
cook that scene first (`./cook_scenes.ps1 -All` does).

```powershell
./cook_scenes.ps1 -Scene sdk-furniture -NoTUI
./cook_scenes.ps1 -Scene surface-overrides -NoTUI
./play_scene.ps1 surface-overrides
```

## What each row shows

Rows run away from the camera over a 400 m floor.

| Row | Instance                    | Override                                            | Expect                                       |
| --- | --------------------------- | --------------------------------------------------- | -------------------------------------------- |
| 1   | `Lamp_Asset`                | None                                                | Metal and shade with the model's materials   |
| 1   | `Lamp_SceneMaterial`        | Scene: shade material red                           | Red shade                                    |
| 1   | `Lamp_ShadeHidden`          | Script: shade Visibility off                        | No shade and no shade shadow                 |
| 1   | `Lamp_MetalNoShadow`        | Script: metal Cast Shadows off                      | Only the shade casts a shadow                |
| 2   | `Pot_Asset`                 | None                                                | Two wood tones                               |
| 2   | `Pot_SceneMaterials`        | Scene: light wood teal, dark wood gold              | Teal and gold                                |
| 2   | `Pot_Runtime`               | Script: dark wood teal, light wood hidden           | Only the teal part                           |
| 3   | `Pillar_Asset`              | None; the asset casts no shadow                     | Pillar without a shadow                      |
| 3   | `Pillar_CastOverride`       | Script: Cast Shadows on                             | Pillar with a shadow                         |
| 3   | `Ghost_Asset`               | None; the asset is hidden                           | Nothing                                      |
| 3   | `Ghost_VisibleOverride`     | Script: Visibility on                               | Blue sphere                                  |
| 3   | `Lod_Asset`                 | None; the asset's distance policy                   | Green sphere near, yellow, then red cube far |
| 3   | `Lod_Fixed2`                | Script: fixed LOD 3                                 | Red cube at any distance                     |
| 3   | `Lod_ScreenSpaceError`      | Script: screen-space error policy                   | LOD by on-screen size                        |
| 3   | `Lod_Material`              | Script: one material on the surface every LOD binds | Purple at every LOD                          |
| 4   | `Lamp_SceneShadeHidden`     | Scene: shade Visibility off                         | No shade and no shade shadow                 |
| 4   | `Lamp_SceneMetalNoShadow`   | Scene: metal Cast Shadows off                       | Only the shade casts a shadow                |
| 4   | `Pillar_SceneCast`          | Scene: Cast Shadows on                              | Pillar with a shadow                         |
| 4   | `Ghost_SceneVisible`        | Scene: Visibility on                                | Blue sphere                                  |
| 4   | `Lod_SceneFixed`            | Scene: fixed LOD 3                                  | Red cube at any distance                     |
| 4   | `Lod_SceneScreenSpaceError` | Scene: screen-space error policy                    | LOD by on-screen size                        |
| 5   | `Play_Lamp`, `Play_Lod`     | Keys below                                          | Changes as you press them                    |

The LOD chain is one surface bound in three LODs, each with its own colour, so
the colour shows the LOD in use. Fly the camera toward and away from row 3 to
see the distance and screen-space error policies switch.

## Keys

| Key | Playground change                                                 |
| --- | ----------------------------------------------------------------- |
| 1   | Shows or hides the lamp's shade                                   |
| 2   | Turns the lamp metal's shadow off or on                           |
| 3   | Cycles the shade material: model, red, blue                       |
| 4   | Cycles the LOD policy: asset, fixed LOD 1/2/3, screen-space error |
| 0   | Clears every playground override                                  |
