# ED-M06A Game Project Layout And Template Standardization Detailed Plan

Status: `accepted`

## 1. Purpose

Make the game project filesystem contract real before content pipeline,
runtime parity, and V0.1 acceptance work build on it.

ED-M06A standardizes newly created project structure, predefined project
templates, scene/material creation targets, project-root browsing, authored
mount handling, local folder mounts, source media positioning, and derived-root
presentation. It closes the gap between the accepted game-project architecture,
the project-layout LLD, and the current project/template/content-browser
behavior.

This milestone exists because ED-M07 depends on stable project layout and asset
identity. Cook, mount, inspect, and standalone validation must not be built on
stale template payloads, scene files outside authored content roots, or browser
targets that confuse source files with derived output.

## 2. PRD Traceability

| ID | ED-M06A Coverage |
| --- | --- |
| `GOAL-001` | Newly created projects open into a valid workspace with an authored starter scene. |
| `GOAL-004` | Authored material and scene assets use stable asset identity under declared content roots. |
| `GOAL-005` | Project layout separates authored content, source media, config, and derived output. |
| `GOAL-006` | Template, mount, layout, and content-root failures produce visible diagnostics. |
| `REQ-002` | Project create/open workflows handle valid and invalid projects. |
| `REQ-017` | Project content roots and mounts define asset identity. |
| `REQ-018` | Cooked output is separate from authoring data. |
| `REQ-019` | Cooked output is discoverable without becoming an authoring root. |
| `REQ-020` | Content Browser exposes project content roots and project-visible folders coherently. |
| `REQ-021` | Asset references preserve authored identity through mount tokens. |
| `REQ-022` | Project/template/layout failures produce visible operation results. |
| `REQ-024` | Diagnostics identify project layout, mount, template, or browser target causes. |
| `REQ-036` | Content Browser navigation and creation targets are predictable. |
| `REQ-037` | Scene/material files persist stable asset URIs and not browser-only or derived state. |
| `SUCCESS-001` | A new project can be created and opened without manual file repair. |
| `SUCCESS-006` | Content pipeline readiness has a stable project layout and browser foundation. |

## 3. Required LLDs

Only these LLDs gate ED-M06A implementation:

- [project-layout-and-templates.md](../lld/project-layout-and-templates.md)
- [project-services.md](../lld/project-services.md)
- [project-workspace-shell.md](../lld/project-workspace-shell.md), for
  create/open activation behavior only
- [content-browser-asset-identity.md](../lld/content-browser-asset-identity.md)
- [material-editor.md](../lld/material-editor.md), for material create/open
  target behavior only
- [scene-authoring-model.md](../lld/scene-authoring-model.md), for scene file
  identity and persistence shape only
- [diagnostics-operation-results.md](../lld/diagnostics-operation-results.md)

Supporting context:

- [content-pipeline.md](../lld/content-pipeline.md), for derived-root and
  cooked-index positioning only. ED-M06A must not run full cook workflows.
- [ARCHITECTURE.md](../ARCHITECTURE.md) §9.1, for the game project filesystem
  architecture contract and service boundaries.

## 4. Scope

ED-M06A includes:

- Make newly created projects follow the standard game project layout:
  project manifest, `Content`, required authored content subfolders, `Config`,
  source-media folders, and derived roots kept separate from authored content.
- Update predefined templates so every built-in template has:
  - `Template.json`;
  - required icon and preview media;
  - `AuthoringMounts` containing `Content -> Content`;
  - empty `LocalFolderMounts`;
  - starter scene under `Content/Scenes`;
  - starter material under `Content/Materials`.
- Ensure project creation generates a new project id every time and writes the
  final `Project.oxy` after copying template payload.
- Ensure create-from-template validates the created project before activation.
- Ensure starter scene activation opens the template-declared scene under
  `Content/Scenes`.
- Ensure scene create/open/save uses `Content/Scenes/*.oscene.json` by default
  and does not write scenes under top-level `Scenes` or `Content/Generated`.
- Ensure material create/open/save uses `Content/Materials/*.omat.json` by
  default and follows the project-layout target resolver.
- Centralize default authoring target resolution for scene, material, geometry,
  and script creation:
  - project root resolves to `/Content/<KindFolder>`;
  - valid folders inside an authored mount are honored only when they match
    the asset kind;
  - valid authored mount roots resolve to `/<Mount>/<KindFolder>`;
  - explicitly targeted local mounts follow the same kind-folder rules;
  - local mounts not explicitly chosen do not become default creation targets;
  - `Config`, `Packages`, `.cooked`, `.imported`, `.build`, and `.oxygen`
    never become authoring create targets.
- Update Content Browser root and folder behavior so:
  - clicking a folder refreshes the visible row set;
  - project root, authored content roots, source media, config, packages, and
    derived roots have distinct presentation;
  - material rows show one logical row per material descriptor plus derived
    cooked state, not random project files;
  - source-media rows are importable source identity, not runtime asset
    references.
- Update ED-M05 and ED-M06 validation expectations so material picker,
  material create, and content browser behavior are checked against the new
  layout contract before their pending ledger rows can close.
- Add focused tests around template validation, project creation layout,
  target resolution, unique project ids, Content Browser folder refresh, and
  material picker row filtering.

## 5. Non-Scope

ED-M06A does not include:

- Full content pipeline execution, scene cook, package, inspect, mount refresh,
  or standalone runtime validation.
- Texture authoring, material graphs, shader authoring, or source-import UI.
- Runtime material live-application support beyond existing ED-M05/ED-M06
  behavior.
- Multi-project workspaces.
- Repair tools for projects that do not satisfy the V0.1 project contract.
- Broad Content Browser visual redesign outside layout/root/state clarity.
- Engine multi-view or viewport tooling work.

## 5.1 User Workflow Contract

The user-visible project model after ED-M06A:

```text
New Project: Games/Blank
  creates:
    NewBlank/
      Project.oxy        # new project id
      Content/
        Scenes/Main.oscene.json
        Materials/Default.omat.json
        Geometry/
        Textures/
        Audio/
        Video/
        Scripts/
        Prefabs/
        Animations/
        SourceMedia/
      Config/

Content Browser
  Project root
    Content        AUTH
    Config         CONFIG
    .cooked        COOKED
    .imported      DERIVED

New Scene
  writes Content/Scenes/<Name>.oscene.json

New Material
  writes Content/Materials/<Name>.omat.json

Material Picker
  shows material rows only, one row per logical material
  returns asset:///Content/Materials/<Name>.omat.json
```

Folder clicks must update the browser list immediately. The user should never
need to restart the editor to see a material descriptor saved by the Material
Editor.

## 6. Implementation Sequence

### ED-M06A.1 - Planning Lock And Baseline Audit

Goal: enter implementation with the project-layout contract fixed and all
affected code paths known.

Tasks:

- Confirm the architecture and project-layout LLD are accepted together.
- Audit current template payloads and template metadata.
- Audit project creation service and `Project.oxy` write path.
- Audit scene create/open/save paths and starter-scene activation.
- Audit material create/open/save path and material picker refresh.
- Audit Content Browser root tree, folder navigation, row provider, and folder
  selection persistence.
- Audit tests that currently seed projects, scenes, or materials.
- Produce a touch list before source changes start.

Exit:

- No implementation starts until the touch list covers templates, project
  creation, scene creation, material creation, content browser navigation, and
  material picker refresh.

### ED-M06A.2 - Template Descriptor And Payload Standardization

Goal: make predefined templates deterministic and valid.

Tasks:

- Add or update built-in template `Template.json` files with required fields:
  `SchemaVersion`, `TemplateId`, `Category`, `DisplayName`, `Description`,
  `Icon`, `Preview`, `SourceFolder`, `AuthoringMounts`, `LocalFolderMounts`,
  `StarterScene`, and `StarterContent`.
- Ensure icon and preview files exist for every predefined template.
- Ensure predefined template `SourceFolder` payloads do not include
  `Project.oxy`. Project creation writes the final `Project.oxy` after payload
  copy. A stray payload `Project.oxy` rejects the template with a
  `ProjectTemplate` diagnostic.
- Ensure template payloads include the required folder skeleton.
- Ensure starter scenes live under `Content/Scenes`.
- Ensure starter materials live under `Content/Materials`.
- Ensure template payloads do not include authored scenes under top-level
  `Scenes` or `Content/Generated`.

Validation:

- Template validation fails on missing required descriptor fields.
- Template validation fails on missing icon or preview media.
- Template validation fails when `SourceFolder` contains `Project.oxy`.
- Template validation fails when any required folder skeleton entry is missing.
- Creating two projects from the same template produces different project ids.

### ED-M06A.3 - Project Creation And Activation

Goal: make Project Browser create a complete V0.1 game project.

Tasks:

- Update project creation service to copy template payload, ensure required
  folder skeleton, generate a new project id, write final `Project.oxy`, and
  validate before activation.
- Ensure `Project.oxy` has required V0.1 fields and does not persist absolute
  project location, recent usage timestamps, operation results, cooked state,
  or derived-root state.
- Ensure create-from-template uses the template-declared starter scene and
  opens it after workspace activation when `OpenOnCreate` is true. If a
  non-built-in template has no starter scene or sets `OpenOnCreate = false`,
  activation completes with an empty workspace and no diagnostic.
- Ensure project creation failures publish visible operation results with
  template/layout/mount diagnostics.

Validation:

- Project Browser can create each predefined template.
- Created projects contain the required skeleton and a unique project id.
- Created projects open into workspace and show the starter scene where
  declared.
- Failed template validation keeps Project Browser usable and visible.

### ED-M06A.4 - Authoring Target Resolution

Goal: centralize where new authored assets are written.

Tasks:

- Add `IAuthoringTargetResolver` in `Oxygen.Editor.Projects`:
  `ResolveCreateTarget(ProjectContext project, AssetKind assetKind,
  ContentBrowserSelection? selection) -> AuthoringTarget`.
- Use the resolver from scene creation, material creation, and Content Browser
  create actions.
- Implement rules for project root, authoring mount root, matching kind
  folders, non-matching authored folders, explicit local mount targets, and
  non-authoring roots.
- Ensure UI surfaces display the target folder and asset URI before creation
  where a create dialog exists.
- Ensure invalid target resolution produces a visible layout diagnostic.

Validation:

- New scene defaults to `Content/Scenes`.
- New material defaults to `Content/Materials`.
- Selection under `.cooked`, `.imported`, `.build`, `.oxygen`, `Config`, or
  `Packages` does not change the default authoring target.
- Explicit local mount target behavior follows the LLD table.

### ED-M06A.5 - Scene And Material Creation Paths

Goal: remove stale scene/material placement behavior from authoring workflows.

Tasks:

- Update scene creation/open/save to use `Content/Scenes/*.oscene.json` as the
  authored scene source location.
- Ensure new scenes are not saved under `Content/Generated`.
- Ensure starter scene paths and persisted recent document paths use asset
  identity and the new scene source location.
- Update material creation/save to use `Content/Materials/*.omat.json`.
- Ensure material rename policy from ED-M05 still keeps material name and file
  name consistent under the new target resolver.
- Ensure material save publishes a catalog/change notification so Content
  Browser and material picker refresh without editor restart.

Validation:

- Creating a scene writes under `Content/Scenes`.
- Creating a material writes under `Content/Materials`.
- Saved material appears in Content Browser and Material Picker without restart.
- Renaming a material keeps file name, material name, asset URI, and browser
  row consistent.

### ED-M06A.6 - Content Browser Roots And Rows

Goal: make the Content Browser reflect the project layout and not a random
filesystem dump.

Tasks:

- Update root tree projection for project root, authored mounts, local mounts,
  config, packages, and derived roots.
- Ensure folder click/navigation refreshes the visible list for the selected
  folder.
- Ensure material kind filtering returns only material rows.
- Ensure `.import.json`, raw source files, and derived outputs do not appear as
  material picker material choices unless they resolve to a valid material
  descriptor row.
- Ensure descriptor + cooked companion rows merge into one logical asset row.
- Ensure `Content/SourceMedia` rows show source/importable state and do not
  satisfy runtime asset pickers.
- Ensure copy affordances use asset URI for authored content and filesystem
  paths only for explicit path-copy actions.

Validation:

- Clicking `Content/Materials` shows one row per logical material.
- Material picker shows one row per material in the project and no unrelated
  project files.
- Clicking folders updates the visible list without restart.
- `Content/SourceMedia` rows are visible as source/importable, not material
  picker choices.

### ED-M06A.7 - Tests, Manual Validation Script, And Ledger Sync

Goal: close the corrective milestone with real evidence.

Tasks:

- Add/update tests for:
  - template descriptor validation;
  - unique project id generation;
  - required folder skeleton creation;
  - project manifest output fields;
  - authoring target resolution;
  - scene/material default paths;
  - material save refresh notification;
  - Content Browser folder navigation;
  - material picker filtering and row merging.
- Record the manual validation script in this plan and the ledger.
- Update `IMPLEMENTATION_STATUS.md` after implementation lands.
- Keep ED-M05 and ED-M06 validation rows pending until ED-M06A validation
  confirms material/create/browser behavior under the new layout.

Validation:

- Targeted tests pass through MSBuild only.
- User manually validates project creation, scene creation, material creation,
  Content Browser folder navigation, and material picker filtering.

## 7. Project/File Touch Points

Expected primary touch points:

- `design/editor/PLAN.md`
- `design/editor/IMPLEMENTATION_STATUS.md`
- `design/editor/plan/README.md`
- `projects/Oxygen.Editor.ProjectBrowser/src/`
  - template discovery, template metadata, create-from-template UI flow.
- `projects/Oxygen.Editor.Projects/src/`
  - project creation service, project manifest writer, template validator,
    project-layout/target resolver, project validation diagnostics.
- `projects/Oxygen.Editor.Projects/tests/`
  - template/project creation/target resolution tests.
- `projects/Oxygen.Editor.WorldEditor/src/`
  - scene creation/open/save target usage and starter scene activation.
- `projects/Oxygen.Editor.WorldEditor/tests/`
  - scene path and persistence tests where seams exist.
- `projects/Oxygen.Editor.MaterialEditor/src/`
  - material create/save target usage and catalog/change notification.
- `projects/Oxygen.Editor.ContentBrowser/src/`
  - root tree projection, folder navigation, row provider, picker filtering.
- `projects/Oxygen.Editor.ContentBrowser/tests/`
  - folder navigation, reducer/provider/picker tests.
- predefined project template folders:
  - `projects/Oxygen.Editor.ProjectBrowser/src/Assets/Templates/Games/Blank/`
  - `projects/Oxygen.Editor.ProjectBrowser/src/Assets/Templates/Games/First Person/`
  - `projects/Oxygen.Editor.ProjectBrowser/src/Assets/Templates/Visualization/Blank/`

## 8. Dependency And Execution Risks

| Risk | Mitigation |
| --- | --- |
| Template updates are treated as cosmetic files instead of a project-creation contract. | Validate templates through project creation tests and activation flow. |
| Template payload accidentally includes `Project.oxy`. | Reject stray payload manifests, write final `Project.oxy` after copy, and assert two projects from the same template have distinct ids. |
| Scene creation continues to write under old folders. | Use one target resolver for scene creation and assert `Content/Scenes/*.oscene.json`. |
| Material Editor saves a descriptor but Content Browser/picker does not refresh. | Publish catalog/change notification on save and add picker/provider refresh tests. |
| Content Browser shows every file under the project as a material candidate. | Material picker consumes kind-filtered identity rows and tests reject non-material files. |
| Local mount handling silently changes default creation targets. | Local mounts only become create targets when explicitly selected; tests cover explicit and implicit cases. |
| ED-M07 starts before layout correction lands. | Ledger marks ED-M06A as the next prerequisite before ED-M07 implementation. |

## 9. Validation Gates

ED-M06A can close only when:

- Built-in template descriptors validate required fields and required media.
- Built-in template payloads do not contain `Project.oxy`; a stray payload
  manifest rejects the template.
- Creating two projects from the same template produces two different project
  ids.
- Created projects contain the required V0.1 folder skeleton.
- Created `Project.oxy` contains required manifest fields and no runtime-local
  or derived-output state.
- Creating a project opens the declared starter scene under `Content/Scenes`.
- Creating a new scene writes `Content/Scenes/<Name>.oscene.json`.
- Creating a new material writes `Content/Materials/<Name>.omat.json`.
- Material save refreshes Content Browser and Material Picker without editor
  restart.
- Content Browser folder click/navigation refreshes the visible list.
- Material picker shows one row per logical material and no unrelated project
  files.
- Source media rows are visible as source/importable and are not runtime asset
  picker choices.
- `.cooked`, `.imported`, `.build`, `.oxygen`, `Config`, and `Packages` never
  become default authoring create targets.
- Extra authoring mounts and explicit local mounts follow the target resolver
  rules.
- Project/template/layout failures publish visible operation results.

Manual validation script:

1. Launch the editor and create `Games/Blank`.
2. Confirm the created project has the required folders and a unique project
   id in `Project.oxy`.
3. Confirm the workspace opens the starter scene from `Content/Scenes`.
4. Create a new scene and confirm it appears under `Content/Scenes`.
5. Create a new material and confirm it appears under `Content/Materials`.
6. Save the material and confirm Content Browser and Material Picker show it
   without restart.
7. Click several folders in Content Browser and confirm the list refreshes.
8. Open the material picker from Geometry and confirm only material rows appear.
9. Select `.cooked`, `Config`, and `Project Root`; confirm new material target
   remains `/Content/Materials`.

## 10. Status Ledger Hook

When ED-M06A implementation lands, update:

- [../IMPLEMENTATION_STATUS.md](../IMPLEMENTATION_STATUS.md) current focus.
- [../IMPLEMENTATION_STATUS.md](../IMPLEMENTATION_STATUS.md) ED-M06A
  checklist.
- [../IMPLEMENTATION_STATUS.md](../IMPLEMENTATION_STATUS.md) validation ledger
  with one concise ED-M06A evidence row after user manual validation.

The ledger evidence should record:

- templates validated.
- project creation result and unique id behavior.
- scene/material authored paths.
- Content Browser folder navigation behavior.
- Material Picker filtering and refresh behavior.
- target resolver behavior for project root, derived roots, and local mounts.
- MSBuild/test commands used, if run.
