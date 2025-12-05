//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <optional>
#include <string>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Base/Types/Geometry.h>
#include <Oxygen/Core/Types/View.h>
#include <Oxygen/Graphics/Common/Types/Color.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/Renderer/Renderer.h>
#include <Oxygen/Scene/SceneNode.h>

namespace oxygen {
class Graphics;
namespace engine {
class Renderer;
struct RenderContext;
} // namespace engine
namespace graphics {
class CommandRecorder;
class Framebuffer;
class Texture;
class Surface;
} // namespace graphics
namespace scene {
class Scene;
} // namespace scene
} // namespace oxygen

namespace oxygen::interop::module {

class ViewRenderer;
// struct ViewContext; // Removed forward declaration

struct EditorViewContext {
  engine::FrameContext& frame_context;
  Graphics& graphics;
  graphics::CommandRecorder* recorder = nullptr; // Phase-specific!
};

enum class ViewState {
  kCreating,  // Resources being allocated
  kReady,     // Fully initialized, can render
  kHidden,    // Not rendering but resources retained
  kReleasing, // Resources being freed
  kDestroyed  // Fully cleaned up
};

class EditorView {
public:
  struct Config {
    std::string name;
    std::string purpose;
    std::optional<graphics::Surface*> compositing_target;

    // Compositing target dimensions have higher priority. Defaults are 1x1 to
    // prevent invalid textures but still indicate a misconfigured view.
    uint32_t width = 1;   // Fallback width if no compositing target
    uint32_t height = 1;  // Fallback height if no compositing target
    graphics::Color clear_color { 0.1f, 0.2f, 0.38f, 1.0f };

    // Resolves actual extent from compositing target if available, otherwise uses configured width/height
    auto ResolveExtent() const -> SubPixelExtent;
  };

  explicit EditorView(Config config);
  ~EditorView();

  OXYGEN_MAKE_NON_COPYABLE(EditorView)
  OXYGEN_MAKE_NON_MOVABLE(EditorView)

  // Set rendering context (must be called before Initialize)
  void SetRenderingContext(const EditorViewContext& ctx);
  void ClearPhaseRecorder(); // Clear phase-specific pointers after OnSceneMutation

  // Phase hooks
  void Initialize(scene::Scene& scene);
  void OnSceneMutation(); // Uses context from SetRenderingContext
  auto OnPreRender(engine::Renderer& renderer) -> oxygen::co::Co<>;

  // State management
  void Show();
  void Hide();
  void ReleaseResources();

  [[nodiscard]] auto GetViewId() const -> ViewId;
  [[nodiscard]] auto GetState() const -> ViewState;
  [[nodiscard]] auto IsVisible() const -> bool;
  [[nodiscard]] auto GetCameraNode() const -> scene::SceneNode;
  [[nodiscard]] auto GetColorTexture() const -> std::shared_ptr<graphics::Texture> { return color_texture_; }
  [[nodiscard]] auto GetConfig() const -> const Config& { return config_; }

  // Renderer registration
  void RegisterWithRenderer(engine::Renderer& renderer);
  void UnregisterFromRenderer(engine::Renderer& renderer);

  // Render graph customization
  void SetRenderGraph(
      std::shared_ptr<engine::Renderer::RenderGraphFactory> factory);

private:
  // Camera setup helpers (all scene mutations happen here)
  void CreateCamera(scene::Scene& scene);
  void UpdateCameraForFrame();
  Config config_;
  ViewState state_ { ViewState::kCreating };
  bool visible_ { true };
  bool initial_orientation_set_ { false };
  float width_ { 0.0f };
  float height_ { 0.0f };

  scene::SceneNode camera_node_;
  ViewId view_id_ { kInvalidViewId };

  // Resources
  std::shared_ptr<graphics::Texture> color_texture_;
  std::shared_ptr<graphics::Texture> depth_texture_;
  std::shared_ptr<graphics::Framebuffer> framebuffer_;

  // Rendering
  std::unique_ptr<ViewRenderer> renderer_;
  std::shared_ptr<engine::Renderer::RenderGraphFactory> render_graph_factory_;

  std::weak_ptr<Graphics> graphics_;
  std::weak_ptr<scene::Scene> scene_;
  oxygen::observer_ptr<engine::Renderer> renderer_module_ { nullptr };

  // Phase-specific context (valid only during OnSceneMutation)
  const EditorViewContext* current_context_ { nullptr };
};

} // namespace oxygen::interop::module
