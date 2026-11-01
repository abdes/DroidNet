# Oxygen examples project

The engine's examples as an editor project: one `Showcase` mount on the
`Content` folder, raw media in `Content/SourceMedia`, one folder and one
manifest per example, and a catalog
of examples. No tool writes into it: RenderScene's tools cook, package and play
it into RenderScene's own folders, and the editor opens it like any project.
See the [RenderScene user guide](../RenderScene/README.md) for scene selection
and controls, and for
[the tools that cook, package and play the examples](../RenderScene/README.md#cook-package-and-play-the-examples).
Asset attribution is in [ASSET_CREDITS.md](Content/SourceMedia/ASSET_CREDITS.md).

[Layout](#layout) · [Cook, package and play](#cook-package-and-play) ·
[Models](#models) · [Media](#media) · [Add an example](#add-an-example) ·
[Rules](#rules)

## Layout

```text
Examples/Showcase/                # project root
  Project.oxy                     # Mounts: Showcase -> Content
  Examples.json                   # the catalog of examples
  Content/                        # mount Showcase
    Geometry/ Materials/          # parts several examples use
    Scenes/<Example>/             # one folder per example with a scene
    SourceMedia/                  # raw media, never cooked as is
      DCC/ Images/
      showcase-assets.json        # pinned downloads: URL, size, SHA-256
      ASSET_CREDITS.md
  Manifests/<Example>.import-manifest.json
  Tools/                          # fetch; Generators/ and the schemas
  .cooked/ .build/ .mounts.json   # the editor's outputs, ignored
```

| Zone                   | Holds                                                                    |
| ---------------------- | ------------------------------------------------------------------------ |
| `Content/`             | Files written for Oxygen: descriptors, scenes, scripts, input, physics   |
| `Content/SourceMedia/` | Files Oxygen reads but does not define: DCC models, mesh buffers, images |
| `Manifests/`           | One import manifest per example, naming every job the example needs      |
| `Tools/`               | `fetch`, the generators that write sources, and the catalog schemas      |

A cooked identity is its source's mount path with the cooked extension:
`/Showcase/Scenes/BottleOnBox/BottleOnBox.scene.json` cooks to
`/Showcase/Scenes/BottleOnBox/BottleOnBox.oscene`. Descriptors name the files
of this mount by a path relative to themselves
(`../../SourceMedia/Images/Showcase/Sky.hdr`), and the files of another mount by
its mount path (`/Engine/Materials/Default.omat`). A manifest names no
`output`: the tool that runs it chooses the cooked root.

## Cook, package and play

RenderScene owns these commands; each writes only into RenderScene
(`.cooked/Showcase`, `.build/pak/examples.pak`):

```powershell
Examples/RenderScene/Tools/cook BottleOnBox   # cook the named examples
Examples/RenderScene/Tools/cook -Check        # check the project, cook nothing
Examples/RenderScene/Tools/pak                # package what was cooked
Examples/RenderScene/Tools/play BottleOnBox   # play an example's first scene
```

`cook -Check` cooks nothing: every example has its manifest and scenes, and
ImportTool's source analysis (`analyze-sources`) reads every manifest and the
sources it names, reporting what the import rules refuse. The
[RenderScene guide](../RenderScene/README.md#cook-package-and-play-the-examples)
lists every option.

## Models

A model import record holds a model's source and settings, and what earlier
imports established. It is the model's authored file and lives at the model's
identity in its mount: the model `/Showcase/Models/Showcase/chair.fbx` has the
record `Content/Models/Showcase/chair.fbx.import.json`, committed with the
content. The identity is where the record lives, so the record never repeats
it. Its `source` names the source file relative to the record when they share
a mount (`../../SourceMedia/DCC/Showcase/chair.fbx`), and by mount path
otherwise. A manifest's `model` job names the model by `virtual_path`, so
cooking an example cooks the models it uses.

A model of one's own stays local: its record is never committed here. The
[RenderScene guide](../RenderScene/README.md#cook-package-and-play-the-examples)
says where RenderScene keeps ad-hoc imports.

## Media

From the project root:

```powershell
Tools/fetch          # download what is missing or differs
Tools/fetch -Check   # only check
```

`Content/SourceMedia/showcase-assets.json` pins each downloaded file: its path,
URL or archive, size and SHA-256. Credits are in `ASSET_CREDITS.md` beside it.

## Add an example

1. Create `Content/Scenes/<Name>/` with its descriptors and scene.
2. Add `Manifests/<Name>.import-manifest.json`, naming every job the example
   needs, shared ones included.
3. Add one entry to `Examples.json`.
4. Run `Examples/RenderScene/Tools/cook -Check`, then `cook <Name>`.

No CMake list, tool or test changes. The SDK ships the examples tagged
`showcase`, as this same project reduced to them (`share/oxygen/Showcase`).

## Rules

- **An ordinary project.** No engine tool, runtime or editor code knows these
  examples; anything they need, every project gets. Nothing writes into it but
  the editor and its authors.
- **Shared parts.** A file two examples use lives in `Content/Geometry` or
  `Content/Materials`, and each example's manifest names its job. A shared
  file is never edited for one example; a variation is a copy in the example's
  folder.
- **Generators are tools.** `Tools/Generators/generate_city.py` rewrites
  `CityEnvironmentValidation`; `UvProbe.py`, `SurfaceCar.py` and `Vehicle.py`
  (Blender) write raw media.
- **Model naming.** A model's products are named under its identity
  (`/Showcase/Models/chest.glb/chest.oscene`) with their authored names; only
  characters a path segment refuses are replaced, and names that collide after
  case folding get numeric suffixes.
