# Multi-View Rendering Example

Demonstrates **Phase 2 Multi-View Support** using the new `PrepareView`/`RenderView` APIs.

## Features

- **Two simultaneous views** rendering the same scene with different cameras
- **Main View**: Full-screen perspective of a green sphere
- **PiP View**: Picture-in-picture (25% size, top-right corner) showing the same sphere from a different angle
- **Per-view isolation**: Each view has independent camera, viewport, and scissor rectangles
- **Zero-copy architecture**: `PreparedSceneFrame` is reused per frame, shared via `observer_ptr`

## Phase 2 Implementation

This example showcases the multi-view architecture:

### View Setup (OnSceneMutation)

```cpp
// Main view: full screen
main_camera_view_ = std::make_shared<CameraView>(params, surface);
main_view_id_ = context.AddView(ViewContext { ... });

// PiP view: top-right quarter
pip_camera_view_ = std::make_shared<CameraView>(params, surface);
pip_view_id_ = context.AddView(ViewContext { ... });
```

### Rendering (OnCommandRecord)

```cpp
// Main view
const auto view = main_camera_view_->Resolve();
renderer->PrepareView(main_view_id_, view, context);
co_await renderer->RenderView(main_view_id_, render_lambda, render_context, context);

// PiP view
const auto view = pip_camera_view_->Resolve();
renderer->PrepareView(pip_view_id_, view, context);
co_await renderer->RenderView(pip_view_id_, render_lambda, render_context, context);
```

## Future: Phase 3

Phase 3 will add:

- **Wireframe toggle**: PiP view will render in wireframe mode via pass flags
- **Compositing**: Final composition of multiple views into a single output
- Currently both views render solid (demonstrating per-view isolation)

## Building

```bash
cmake --build cmake-build-relwithdebinfo --target oxygen-example-multiview
```

## Running

```bash
./bin/Oxygen/oxygen-example-multiview
```

## Controls

- **ESC**: Exit application
- Window is resizable (views will adapt)

## Architecture Notes

- Uses `ExampleModuleBase` from `Examples/Common`
- Scene created once with a single sphere entity
- Two cameras positioned at different locations
- `RenderGraph` shared between views (same render passes)
- `PerViewState` maintains per-view `PreparedSceneFrame` values
- `RenderContext` gets `observer_ptr<const PreparedSceneFrame>` for zero-copy access
