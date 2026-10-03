# Progress

Design details: [PROPERTY_EDITING.md](PROPERTY_EDITING.md), `F:\projects\oxygen-editor-design\README.md` and `src/brief-content.ts`.

- **Completed:** Arithmetic Transform editing is in place. NumberBox supports optional/colored compact labels, label-only scrubbing, wheel scrolling, and click-away completion; VectorBox supports optional XYZ/RGB labels and colors. Controls demos show Transform and Linear RGB examples. InPlaceEdit and DemoApp MSBuild analyzer builds pass without warnings.
- **Completed:** Scene Inspector property search, scope filters, mode applicability and source-backed atmosphere controls are integrated. Shared PropertyCard and VectorBox layouts adapt to panel width and text scaling without replacing numeric controls; disclosures remain neutral. Environment and Directional Light models expose linear authoring values, with UI presentation and scheduling supplied at the boundary.
- **Verification:** 90 headless authoring/history/scheduling cases, 117 native Inspector UI cases, 40 native field/persistence cases and 75 numeric-control cases pass. Native scalar-label drags cover commit, undo/redo, cancellation and responsive reflow. Label hover preserves font metrics, geometry and RGB brushes. The runnable editor builds with MSBuild.
- **Next:** Complete workspace-wide visual acceptance and qualify the remaining density and proposed-field scenarios.
