# Oxygen ImGui: Architecture, Backends, and Assets

This directory provides the engine's ImGui module, UI assets and generic
widgets. It links engine, platform, console and graphics-common, never
Vortex; Vortex has no ImGui dependency.

## Why This Exists

ImGui state is global per ImGui runtime instance (`GImGui`).
In multi-DLL builds, each DLL can end up with a different ImGui global unless
the runtime is unified. This affects how APIs must be designed and called.

## Runtime Architecture

- `Oxygen.Graphics.Direct3D12`:
  owns D3D12 ImGui backend implementation (`D3D12ImGuiGraphicsBackend`),
  creates and destroys the ImGui context, and renders draw data.
- `Oxygen.Platform`:
  knows nothing about ImGui. It offers a backend-independent UI surface: a UI
  event filter (`Platform::RegisterUiEventFilter`, fed with `platform::UiEvent`),
  per-window size and display scale, and `Platform::Ui()` for the clipboard,
  mouse cursor, mouse capture and text input placement.
- `Oxygen.ImGui`:
  owns `ImGuiModule` (context binding, window binding, frame start/end,
  style/fonts), the platform backend (`PlatformBackend`), UI assets and generic
  widgets. It never touches the windowing backend (SDL, Win32...) directly.
- `Oxygen.Vortex`:
  knows only surface layer sources
  ([multi-view composition §12.4](../../../design/vortex/lld/multi-view-composition.md#124-surface-layer-sources)):
  it owns the layer texture, records the source into it and blends it over
  the composed surface.
- `Oxygen.Host`:
  `ImGuiLayer` adapts the module to a Vortex surface layer source; it is the
  only code that sees both.

## Lifecycle and Frame Flow

1. The host registers `ImGuiModule` unless it runs headless, and registers
   `ImGuiLayer` with the renderer for the window's surface.
2. `ImGuiModule::OnAttached` creates the backend through
   `Graphics::CreateImGuiGraphicsBackend()` and initializes it (`Init`), which
   creates the ImGui context and initializes `imgui_impl_dx12`, then binds the
   context and applies style/fonts/assets.
3. `ImGuiModule::SetWindowId` creates `PlatformBackend` for a valid window;
   no frame starts while no window is bound.
4. Each frame:
   - `OnFrameStart`: platform backend `NewFrame`, validate display metrics,
     graphics backend `NewFrame` (calls `ImGui::NewFrame`).
   - `OnGuiUpdate`: applications build UI with `GetImGuiContext()`.
   - compositing: Vortex calls `ImGuiLayer::Record`, which delegates to the
     graphics backend `Render`.
   - `OnFrameEnd`: calls `ImGui::EndFrame` if a frame was started.
5. Shutdown: the host unregisters `ImGuiLayer`, then the platform backend
   detaches and the graphics backend shuts down (with deferred GPU-safe
   cleanup when available).

## Platform Backend

`imgui::PlatformBackend` connects one platform window to the ImGui context
using only the platform abstraction, so a different windowing backend needs no
change here. Responsibilities:

- apply DPI scaling (`io.FontGlobalScale` and `style.ScaleAllSizes`) and, each
  frame, the display size, framebuffer scale and time step.
- register as the platform's UI event filter so ImGui sees input first, and
  feed it keys, text, mouse motion, buttons and wheel, and focus changes; the
  modifier state is derived from the modifier keys.
- consume the events ImGui wants (`io.WantCaptureKeyboard`,
  `io.WantCaptureMouse`) so game input never sees them.
- give ImGui the clipboard, set the mouse cursor, capture the mouse during
  drags, and place the text input (IME) at the caret.
- unregister the filter and release capture and text input on destruction.

`Oxygen::Key` is layout based: punctuation keys map to ImGui's by key code, not
by scan code, and keys outside the Oxygen vocabulary (F16 and above, media keys)
are not delivered. Gamepad navigation is not provided.

**Key implementation files:**

- `src/Oxygen/ImGui/PlatformBackend.h`
- `src/Oxygen/ImGui/PlatformBackend.cpp`
- `src/Oxygen/Platform/UiEvent.h` (the neutral event and service types)
- `src/Oxygen/Platform/SDL/UiEvents.cpp`, `UiServices.cpp` (the SDL3
  implementation of them)

## API Design Rules (Cross-DLL Safe)

For new code added outside `ImGuiModule`, prefer context-free APIs.

1. New helper APIs in `Oxygen.ImGui` should accept explicit ImGui objects.

   - `ImGuiStyle&`
   - `ImFontAtlas&`
   - POD config structs

2. New helper APIs should not call global context-dependent functions.

   - avoid `ImGui::GetIO()`, `ImGui::GetStyle()`, `ImGui::SetCurrentContext()`.
   - avoid `ImGui::CreateContext()/DestroyContext()`.

3. `ImGuiModule` binds the context before context-dependent work.

   - fetch context from graphics backend.
   - call `ImGui::SetCurrentContext(ctx)`.
   - then call helpers with explicit references.

Reference shape:

```cpp
ImGui::SetCurrentContext(ctx);
ImGuiStyle& style = ImGui::GetStyle();
oxygen::imgui::spectrum::StyleColorsSpectrum(style);

ImGuiIO& io = ImGui::GetIO();
ImFont* default_font = oxygen::imgui::spectrum::LoadFont(*io.Fonts, 16.0f);
io.FontDefault = default_font;
```

## Context-Bound vs Context-Free APIs in This Module

**Context-free (preferred for new APIs):**

- `Styles/Spectrum.h`
- `StyleColorsSpectrum(ImGuiStyle&)`
- `LoadFont(ImFontAtlas&, float) -> ImFont*`

**Context-bound (existing UI widgets):**

- `Console/ConsolePanel.*` (`Draw(console, state, ImGuiContext*)`)
- `Console/CommandPalette.*` (`Draw(console, state, ImGuiContext*)`)

When using context-bound widgets across DLL boundaries, ensure your runtime
strategy is safe for your linkage model (single shared ImGui runtime or explicit
cross-DLL context strategy). For new reusable assets/helpers, use context-free
patterns.

Current Console widget contract:

- caller passes `ImGuiContext*` from the active ImGui module/backend.
- widget binds it internally with `ImGui::SetCurrentContext(...)` before any
  ImGui calls.
- persisted window placement is applied with `ImGuiCond_Appearing` so windows
  remain user-movable/resizable after opening.

## Assets Offered by `Oxygen.ImGui`

Public headers exported by this module:

- Console UI:
  - `Console/ConsoleUiState.h`
  - `Console/ConsolePanel.h`
  - `Console/CommandPalette.h`
- Icons:
  - `Icons/IconsOxygenIcons.h` (codepoints and UTF-8 literals)
  - `Icons/OxygenIcons.h` (compressed icon font blob)
- Styles and Fonts:
  - `Styles/Spectrum.h` (Spectrum constants + style/font helpers)
  - `Styles/IconsFontAwesome.h` (Font Awesome icon macros/ranges)
  - `Styles/FontAwesome-400.h` (compressed Font Awesome font blob)

Bundled source assets:

- `Styles/SourceSansProRegular.cpp` (embedded default UI font data)
- `Styles/FontAwesome-400.cpp` (embedded Font Awesome data)
- `Icons/OxygenIcons.cpp` (embedded Oxygen icon font data)
- `Icons/svg/*` + `Icons/OxygenIcons.ttf` + mapping/build tooling

## Integration Pointers

- Runtime entry: `ImGuiModule`
- Renderer seam: `vortex::SurfaceLayerSource`, implemented by `ImGuiLayer`
  in `Oxygen.Host`
- Graphics backend interface:
  `src/Oxygen/Graphics/Common/ImGui/ImGuiGraphicsBackend.h`
- D3D12 backend impl:
  `src/Oxygen/Graphics/Direct3D12/ImGui/ImGuiBackend.h`

## Quick Checklist for New Additions

- Is it reusable style/font/icon data? Make it context-free.
- Does it need global ImGui context access? Keep the call site in
  `ImGuiModule`.
- Does it need Vortex data? It belongs in `Oxygen.Host`, which maps the data
  to a widget's own types; widgets here never include Vortex.
- If adding UI widgets in `Oxygen.ImGui`, document linkage/runtime assumptions.
- For context-bound widgets, require `ImGuiContext*` in draw entry points and
  bind it in the widget implementation.
