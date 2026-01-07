# RenderScene UI Architecture

## Overview

The RenderScene demo UI has been restructured into a modular, maintainable architecture where each panel is implemented in its own set of files. This improves code organization, testability, and reusability.

## Directory Structure

```text
Examples/RenderScene/
├── UI/
│   ├── FilePicker.h/cpp              # Reusable file picker dialog
│   ├── FbxLoaderPanel.h/cpp          # FBX file loading & import
│   ├── PakLoaderPanel.h/cpp          # PAK file loading & scene browser
│   ├── LooseCookedLoaderPanel.h/cpp  # Loose cooked index loader
│   ├── CameraControlPanel.h/cpp      # Camera mode & debug controls
│   └── ContentLoaderPanel.h/cpp      # Unified content loader
├── MainModule.h/cpp                  # Main demo module
├── OrbitCameraController.h/cpp       # Orbit camera implementation
└── FlyCameraController.h/cpp         # Fly camera implementation
```

## UI Components

### 1. FilePicker (`UI/FilePicker.h/cpp`)

**Purpose:** Provides a reusable, platform-native file picker dialog.

**Features:**

- Cross-platform abstraction (Windows COM implementation, extensible for other platforms)
- Pre-configured factory functions for common file types (PAK, FBX, Index)
- Clean API with `FilePickerConfig` struct

**Usage:**

```cpp
auto picker_config = ui::MakePakFilePickerConfig();
picker_config.initial_directory = content_root / "pak";

if (const auto selected_path = ui::ShowFilePicker(picker_config)) {
    LoadPakFile(*selected_path);
}
```

### 2. FbxLoaderPanel (`UI/FbxLoaderPanel.h/cpp`)

**Purpose:** Loads and imports FBX files into the cooked asset format.

**Features:**

- Auto-scans FBX directory for available files
- Manual file selection via file picker
- Asynchronous import with progress indicator
- Automatic scene extraction and loading after import
- Refresh button to rescan directory

**Configuration:**

```cpp
ui::FbxLoaderConfig config;
config.fbx_directory = content_root / "fbx";
config.cooked_output_directory = content_root / ".cooked";
config.on_scene_ready = [](const data::AssetKey& key) {
    // Load the imported scene
};

fbx_panel.Initialize(config);
```

### 3. PakLoaderPanel (`UI/PakLoaderPanel.h/cpp`)

**Purpose:** Loads PAK files and displays available scenes.

**Features:**

- Auto-scans PAK directory for available files
- Manual PAK selection via file picker
- Scene browser with virtual path display
- PAK mount integration with asset loader
- Unload/reload functionality
- Refresh button to rescan directory

**Configuration:**

```cpp
ui::PakLoaderConfig config;
config.pak_directory = content_root / "pak";
config.on_scene_selected = [](const data::AssetKey& key) {
    // Load selected scene
};
config.on_pak_mounted = [](const std::filesystem::path& path) {
    asset_loader->AddPakFile(path);
};

pak_panel.Initialize(config);
```

### 4. LooseCookedLoaderPanel (`UI/LooseCookedLoaderPanel.h/cpp`)

**Purpose:** Loads loose cooked asset index files and displays available scenes.

**Features:**

- Auto-discovery from `.cooked` directory
- Manual index file selection via file picker
- Scene browser with asset key tooltips
- Mount integration with asset loader
- Display of total asset count
- Unload/reload functionality

**Configuration:**

```cpp
ui::LooseCookedLoaderConfig config;
config.cooked_directory = content_root / ".cooked";
config.on_scene_selected = [](const data::AssetKey& key) {
    // Load selected scene
};
config.on_index_loaded = [](const std::filesystem::path& path) {
    asset_loader->AddLooseCookedRoot(path.parent_path());
};

loose_panel.Initialize(config);
```

### 5. CameraControlPanel (`UI/CameraControlPanel.h/cpp`)

**Purpose:** Provides ergonomic camera control and debugging interface.

**Features:**

- **Camera Mode Tab:**
  - Toggle between Orbit and Fly camera modes
  - Orbit mode: Trackball vs Turntable selection
  - Fly mode: Speed adjustment slider
  - Control hints for each mode
  - Reset camera button

- **Debug Tab:**
  - Real-time camera pose (position, forward, up, right vectors)
  - World space alignment indicators (dot products)
  - Input action states (W/A/S/D/Shift/Space/RMB)
  - Mouse delta display
  - ImGui capture state

**Configuration:**

```cpp
ui::CameraControlConfig config;
config.active_camera = &active_camera_;
config.orbit_controller = orbit_controller_.get();
config.fly_controller = fly_controller_.get();
config.move_fwd_action = move_fwd_action_.get();
// ... other action pointers

config.on_mode_changed = [](ui::CameraControlMode mode) {
    // Update input context
};
config.on_reset_requested = []() {
    // Reset camera to initial pose
};

camera_panel.Initialize(config);
```

### 6. ContentLoaderPanel (`UI/ContentLoaderPanel.h/cpp`)

**Purpose:** Unified panel combining all content loading options.

**Features:**

- Single window with tabbed interface
- Manages all loader panels internally
- Simplified initialization with single config struct
- Automatic panel coordination

**Configuration:**

```cpp
ui::ContentLoaderPanel::Config config;
config.content_root = FindRenderSceneContentRoot();
config.on_scene_load_requested = [](const data::AssetKey& key) {
    // Queue scene for loading
};
config.on_pak_mounted = [](const std::filesystem::path& path) {
    asset_loader->AddPakFile(path);
};
config.on_loose_index_loaded = [](const std::filesystem::path& path) {
    asset_loader->AddLooseCookedRoot(path.parent_path());
};

content_loader.Initialize(config);
```

## Integration Pattern

The MainModule uses a simplified integration pattern:

```cpp
class MainModule {
private:
    // UI panels
    ui::ContentLoaderPanel content_loader_panel_;
    ui::CameraControlPanel camera_control_panel_;

    auto InitializeUIPanels() -> void;
    auto UpdateUIPanels() -> void;    // Called once per frame
    auto DrawUI() -> void;            // Called during ImGui rendering
};
```

**Initialization** (in `OnAttached`):

```cpp
InitializeUIPanels();  // Configure all panels with callbacks
```

**Update Loop** (in `OnSceneMutation`):

```cpp
UpdateUIPanels();  // Update async operations (e.g., FBX import)
```

**Rendering** (in `OnGuiUpdate`):

```cpp
DrawUI();  // Draw all panels
```

## Design Principles

1. **Separation of Concerns:** Each panel handles one specific task
2. **Callback-Based Integration:** Panels use callbacks instead of tight coupling
3. **Self-Contained Logic:** Each panel manages its own state
4. **Reusable Components:** FilePicker can be used by any panel
5. **Clean APIs:** Configuration structs make initialization clear
6. **Ergonomic UX:** Professional layout with tooltips, icons, and feedback

## Benefits

- **Maintainability:** Easy to modify individual panels
- **Testability:** Each panel can be tested independently
- **Reusability:** Panels can be used in other demos
- **Readability:** Clear, focused code in each file
- **Extensibility:** Easy to add new content sources or features

## Future Enhancements

Potential improvements to the UI system:

1. **GLB/GLTF Loader Panel:** Similar to FBX loader
2. **Asset Browser Panel:** Browse all assets in mounted sources
3. **Scene Hierarchy Panel:** Tree view of loaded scene
4. **Material Editor Panel:** Edit material properties
5. **Light Editor Panel:** Adjust light parameters
6. **Performance Stats Panel:** FPS, frame times, memory usage
7. **Render Settings Panel:** Toggle debug visualizations
8. **Screenshot/Recording Panel:** Capture and export

## Notes

- Windows-specific file picker uses COM (IFileOpenDialog)
- Other platforms return `std::nullopt` from `ShowFilePicker` (TODO: implement)
- All UI files follow Oxygen Engine coding standards
- Comprehensive documentation in each header file
