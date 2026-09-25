# Oxygen SDK sample content

This directory contains ready-to-run cooked scenes, their editable source files,
and commands to rebuild them with the SDK's native tools. You do not need an
Oxygen checkout, Conan, Python or a developer shell to cook and pack this content.

The commands below run from the **SDK root**, the directory containing `bin` and
`RenderScene.cmd`. Paths with spaces are supported. The CMD entry points use
Windows PowerShell; equivalent `.ps1` entry points are also provided.

## Launch first, then experiment

```powershell
.\RenderScene.cmd
```

The first launch selects the Lantern model with an HDR sky. The Library panel
lets you explore the other shipped scenes. See the
[RenderScene guide](../RenderScene/README.md) for controls and scene selection.

| Sample folder                                        | What to explore                                                         |
| ---------------------------------------------------- | ----------------------------------------------------------------------- |
| `sdk-lantern`                                        | A textured glTF model: material response, lighting and texture detail   |
| `sdk-furniture`                                      | Small FBX furniture samples: table, chair, lamp and plant               |
| `sdk-materials`                                      | Marble and wood color, normal and roughness maps on procedural geometry |
| `city-environment-validation`                        | Atmosphere, fog and sunlight over a procedural skyline                  |
| `emissive`                                           | Emissive materials                                                      |
| `physics_domains`                                    | Bodies, contacts, joints, character and vehicle demonstrations          |
| `multi-script`                                       | Scripted scene behavior and input                                       |
| `point-shadow-validation` / `spot-shadow-validation` | Local-light shadows                                                     |

The source assets are intentionally small. Model and texture credits are in
[ASSET_CREDITS.md](ASSET_CREDITS.md). The archive also includes selected raw FBX,
glTF, HDR and material inputs for trying the import tools yourself.

## Edit and cook a scene

Edit a descriptor under `scenes/sdk-materials`, then rebuild that sample:

```powershell
.\share\oxygen\Content\cook_scenes.cmd -Scene sdk-materials -NoTUI
.\bin\Oxygen.Cooker.Inspector.exe validate .\share\oxygen\Content\.cooked
.\RenderScene.cmd --scene SdkMaterials
```

Cook every shipped manifest with:

```powershell
.\share\oxygen\Content\cook_scenes.cmd -All -NoTUI
```

The scripts automatically use the ImportTool in this SDK's `bin` directory.
They do not search another checkout or choose a different build configuration.
Manifest sources resolve relative to the manifest; descriptor image/buffer
sources resolve relative to their descriptor. Keep these relative relationships
when adding your own files. Use unique asset names and virtual paths for new assets.

Loose cooked output is written to `Content/.cooked`. The native ImportTool
validates the manifests and descriptors; authoring schemas are installed in
[`../schemas`](../schemas). Each manifest's `output` determines its output root.

## Build and inspect a PAK

After cooking, package the loose cooked root:

```powershell
.\share\oxygen\Content\pak_content.cmd
.\bin\Oxygen.Cooker.PakDump.exe --help
```

The default outputs under `Content/pak` are `all.pak`, `all.catalog.json` and
`all.manifest.json`. In RenderScene, use Library to mount the new PAK. Avoid
simultaneously mounting loose and PAK versions of the same assets when comparing
changes. Close RenderScene before replacing an archive it has mounted.

The PAK source key identifies this sample content line. Keep it when rebuilding
the same line; choose a new source key for an independent content package.
`pak_content.cmd -Help` describes the output-name, source-key and report options.

## Import your own content

RenderScene's import panel accepts FBX, glTF/GLB and images. For repeatable work,
copy a sample manifest and change its source paths, names and output as needed.
Use the bundled tools directly for additional workflows:

```powershell
.\bin\Oxygen.Cooker.ImportTool.exe --help
.\bin\Oxygen.Cooker.PakTool.exe --help
.\bin\Oxygen.Cooker.Inspector.exe --help
```

The cooking script's `-ContentRoot` option selects another directory containing
`scenes/<name>/import-manifest.json`. `-ToolPath` can select an explicit compatible
executable; the default uses this SDK. Preset/build-tree switches are for engine
source checkouts, not installed SDK use.

## Troubleshooting

- **Missing source:** inspect the manifest's relative path and copy all referenced
  textures, buffers and scripts together with the scene.
- **Schema validation error:** use the matching schema in this SDK and the native
  tool's diagnostic. A newer SDK may require recooking old descriptors.
- **Scene not found:** validate the cooked index and select the intended content
  source in RenderScene. Successfully cooking a mesh is not the same as creating
  a scene that references it.
- **File locked:** close applications using the cooked root or PAK before rebuilding.
- **Write failure:** this portable SDK currently needs writable content and
  showcase directories. Keep your modified source files and settings when updating.

The scripts return nonzero exit codes on failure. For a clean recook, preserve
your inputs and any output you need, then move the previous `.cooked` and `pak`
directories aside before cooking and packing again.
