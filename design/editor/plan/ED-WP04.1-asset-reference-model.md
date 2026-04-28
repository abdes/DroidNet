# ED-WP04.1 - Asset Reference Model

Status: `planned`

## 1. Goal

Replace raw cooked-path thinking with a durable asset reference model usable by
scene components, the content browser, cooking, validation, and live sync.

## 2. Reference Kinds

- source asset
- import descriptor
- generated procedural descriptor
- cooked virtual path
- unresolved/missing reference

## 3. Required Services

| Service | Responsibility |
| --- | --- |
| Asset catalog | Lists known source, descriptor, generated, and cooked assets. |
| Asset resolver | Resolves authoring references to current asset records. |
| Asset picker | UI service for type-filtered reference selection. |
| Reference validator | Reports missing/stale/unsupported references. |
| Cook mapper | Converts authoring references to descriptor/manifest entries. |

## 4. Likely Touch Points

- `Oxygen.Editor.World` asset reference fields
- `Oxygen.Editor.ContentBrowser`
- `GeometryViewModel`
- explicit content-pipeline cook orchestration
- `Oxygen.Assets`

## 5. Acceptance Criteria

- Geometry component can distinguish source/descriptor/cooked/generated/missing.
- Asset picker filters by expected asset type.
- Broken references survive save/reopen and appear as validation issues.
- Cooking can map valid references to manifest entries.
- Live sync can resolve cooked runtime references after mount.

## 6. Risks

- Existing code may assume `AssetReference<T>` URI is enough.
- Cooked virtual paths and project-local paths need a clear canonical form.
- Generated descriptors need stable identity across save/reopen.
