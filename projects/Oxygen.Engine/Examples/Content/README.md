# Oxygen example content

This directory owns the native example content workflow: scene authoring, model
imports, cooked output, validation and packaging. RenderScene consumes it through
Library; see its [user guide](../RenderScene/README.md) for scene selection and
controls. Asset attribution is in [ASSET_CREDITS.md](ASSET_CREDITS.md).

[Paths and tools](#paths-and-tools) · [Cook scenes](#cook-maintained-scenes) ·
[Import models](#import-and-reimport-models) · [Verify and package](#verify-and-package) ·
[Cleanup](#cleanup-and-format-changes)

## Paths and tools

Run the commands below from this **Content directory**:

- Checkout: `projects/Oxygen.Engine/Examples/Content`.
- Installed SDK: `share/oxygen/Content`.

| Path under Content                       | Owner and purpose                                                            |
| ---------------------------------------- | ---------------------------------------------------------------------------- |
| `scenes/<name>/import-manifest.json`     | Authored scene recipes and their source descriptors                          |
| `imports/<name>.import.json`             | Native retained model recipe, source/slot identities and selected generation |
| `.cooked/main/container.index.bin`       | Shared cooked scenes from `cook_scenes.ps1`                                  |
| `.cooked/imports/<source>/<generation>/` | Immutable external glTF/GLB/FBX generations                                  |
| `pak/`                                   | Packaged shared scenes, catalog and manifest                                 |
| `import-sources.local.json`              | Optional ignored list of original model paths for applying new recipes       |

`.cooked` itself is a container directory, **not a cooked root**. Mount the main
index or a retained `.import.json` record. Never mount old generation indexes
alongside their records. Records follow successful publication on the next mount;
existing readers retain their current generation until they switch.

All scripts use the shared `BuildSelection.ps1` launcher. Installed SDK scripts
use that SDK's bundled tools. Checkout scene cooking and packaging select an
existing build, preferring Release; `-BuildTree`, `-Preset` and `-Config` constrain
it. Model imports build a matched **Release** ImportTool/Inspector pair first,
including missing binaries, and stop on build failure before publication.

After changing native code or a cooked format, build the current Release tools
before scene cooking and packaging. `-ToolPath` selects an explicit current tool
without building; model imports also require its sibling Inspector. The caller
owns freshness for explicit paths. Do not combine `-ToolPath` with build-selection
switches. Keep tools and runtime from the same checkout/configuration or SDK.

`cook_scenes.cmd` and `pak_content.cmd` use Windows PowerShell. `import_models.cmd`
launches PowerShell 7.4 or newer (`pwsh`). Every script supports `-Help`/`-h` before tool
selection; model imports and packaging also support `-WhatIf`.

## Cook maintained scenes

Close readers of the mutable main library, edit the authored descriptors, then:

```powershell
./cook_scenes.ps1 -Scene sdk-materials -NoTUI
./cook_scenes.ps1 -All -NoTUI
```

`-All` processes each `scenes/*/import-manifest.json` in name order, including its
materials, geometry, textures, scripts, inputs and physics sidecars. It does not
reimport retained external models or build PAKs. The checkout has sixteen scene
manifests; the SDK ships a smaller selection. Unreferenced raw models and images
are imported explicitly by their consumers.

Sources resolve relative to their manifest or descriptor. Shared recipes write
to `.cooked/main`; external script source roots are declared in the manifests.
Keep those relative relationships when copying inputs. Native schemas validate
recipes; distinct authored assets need distinct names and virtual paths.

## Import and reimport models

For existing registered models, replay their saved recipes and identities:

```powershell
./import_models.cmd -All
./import_models.cmd -Record ./imports/town4new.import.json
```

For a new model, copy the [source-list example](import-sources.example.json), edit
its names and original paths, then apply it:

```powershell
Copy-Item ./import-sources.example.json ./import-sources.local.json
./import_models.cmd -SourceList ./import-sources.local.json -WhatIf
./import_models.cmd -SourceList ./import-sources.local.json
```

Relative paths resolve beside the source list. Keep glTF external buffers and
images alongside their original model. Originals may live outside Content and
are never modified. Records and cooked generations are owned by Content. The
machine-local list is only for creating or deliberately changing recipes;
`-Record` and `-All` replay the retained recipes without applying new defaults.

New/applied recipes default to fast BC7, box-filtered full mip chains, sRGB color
textures and linear data textures. They import all content, normalize units, bake
transforms, generate missing normals/tangents, retain nodes and normalize names.
Quality switches apply only with `-SourceList`:

| Option                               | Choices                                                          |
| ------------------------------------ | ---------------------------------------------------------------- |
| `-Compression`                       | `BC7` (default), `None` (RGBA8 color/data)                       |
| `-MipPolicy`                         | `Full` (default), `None`, `Max` with `-MaxMipLevels`             |
| `-BC7Quality`                        | `Fast` (default), `Default`, `High`; BC7 only                    |
| `-MipFilter`                         | `Box` (default), `Kaiser`                                        |
| `-ThreadPoolSize`, `-TextureWorkers` | Execution budgets; defaults up to 16 threads / 8 texture workers |

Each source imports sequentially and publishes independently. Native publication
validates the candidate and atomically selects its generation and provenance.
Failures keep the previous selection. The script then checks the complete native
file inventory, including digests, and fails if any issue is reported. Temporary
recipes/reports are removed. Camera, environment and other demo settings are not
part of this workflow.

Keep the authored record across recooks: it owns stable source and material-slot
identity. Do not edit publication fields, duplicate records to create independent
assets, or hand-write cooked files. Mount the printed record through RenderScene's
Library. Interactive model imports use the same Content records and generations.

## Verify and package

Select the matching Inspector once. From a checkout Content directory:

```powershell
$inspector = '../../out/build-ninja/bin/Release/Oxygen.Cooker.Inspector.exe'
```

From an installed SDK Content directory:

```powershell
$inspector = '../../../bin/Oxygen.Cooker.Inspector.exe'
```

Verify the shared library; keep reports outside its cooked root:

```powershell
& $inspector inventory ./.cooked/main --output "$env:TEMP/oxygen-main-inventory.json"
& $inspector validate ./.cooked/main
./pak_content.ps1
```

Require successful exit codes **and an empty `issues` array** in the inventory.
`validate` checks native structural contracts; `inventory` also verifies every
owned file's size and SHA-256 and rejects unexpected files. Use the same commands
with a selected retained generation when inspecting it directly.

Packaging reads `.cooked/main` and resolves external script sources against
Content. Default outputs are `pak/all.pak`, `all.catalog.json` and
`all.manifest.json`. `-ContentRoot` selects another authored Content directory and
its default cooked/PAK paths. Keep the stable PAK source key when rebuilding the
same content line. Close readers before replacing a mounted PAK; compare one
representation of the assets at a time.

## Cleanup and format changes

Remove unused retained generations through the native owner:

```powershell
./import_models.cmd -All -Reclaim
```

This invokes `ImportTool reclaim --record` for each record, without reimporting.
Native record locks and exclusive generation leases protect selected, mounted
and in-flight generations. Repeat after old readers close if necessary. It does
not remove authored records or originals, and it never deletes `.cooked/main`.

After a cooked-format change, recook maintained scenes and replay retained
records with current tools, verify them, then reclaim old generations. If main
itself has an incompatible index or stale files, close its readers and remove
**only `.cooked/main`**, then recook all manifests and verify again. Never delete
the enclosing `.cooked` directory: it also owns retained libraries. Rebuild PAKs
when their inputs or format change.

Failures and diagnostics belong to the native tools. Missing-source errors need
the original model and dependencies; incompatible-index errors need a recook;
locked-generation cleanup waits for its readers.

## Contributor notes

The public entry points are `cook_scenes`, `import_models` and `pak_content`,
each with CMD and PowerShell forms. Shared implementation and checkout staging
helpers live under `internal/`; CMake stages public workflows and authoring
inputs into the SDK. `pak_content` packages cooked roots through the native
Cooker APIs. The SDK showcase uses a separate generated directory in
the build tree. When its inputs or tools change, CMake discards that directory's
cooked output and PAKs before recooking; authored files and the installed SDK
remain untouched until the build and install succeed.

The two GLBs in `glb/` belong to rotating-gltf. Raw FBX models and standalone
images outside scene manifests are explicit import/runtime demonstration inputs.
Distinct authored assets need distinct virtual paths: instancing uses
`GeoInstancedCube`, and physics uses `Physics/Materials/physics_domains_ground.opmat`.
Scene descriptors use version 9. Atmosphere sources use explicit per-light slots;
scripted light changes apply validated whole-candidate updates.

The native import naming service keeps normalized spelling and adds numeric
suffixes for mesh, material and scene names that collide after ASCII case folding.
For example, `MetalGrey` and `Metalgrey` become `M_MetalGrey` and `M_Metalgrey_1`.
Namespaces and occupied suffixes participate in collision checks. Scene-node
display names retain their case-sensitive behavior.
