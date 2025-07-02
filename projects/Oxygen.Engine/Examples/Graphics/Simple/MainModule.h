//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <type_traits>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/StaticVector.h>
#include <Oxygen/Graphics/Common/Constants.h>
#include <Oxygen/Graphics/Common/NativeObject.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/Renderer/RenderContext.h>
#include <Oxygen/Renderer/RenderItem.h>

namespace oxygen {
namespace platform {
  class Platform;
  class Window;
} // namespace platform
class Graphics;
namespace graphics {
  class CommandRecorder;
  class RenderTarget;
  class RenderController;
  class Surface;
  class Framebuffer;
  class Buffer;
} // namespace graphics
namespace co {
  class Nursery;
} // namespace co
namespace engine {
  class Renderer;
} // namespace engine
} // namespace oxygen

namespace oxygen::examples {

class MainModule final {
public:
  MainModule(
    std::shared_ptr<Platform> platform, std::weak_ptr<Graphics> gfx_weak);
  ~MainModule();

  OXYGEN_MAKE_NON_COPYABLE(MainModule)
  OXYGEN_MAKE_NON_MOVABLE(MainModule)

  auto StartAsync(co::TaskStarted<> started = {}) -> co::Co<>
  {
    return OpenNursery(nursery_, std::move(started));
  }

  auto Run() -> void;

private:
  alignas(16) struct SceneConstants {
    glm::mat4 world_matrix { 1.0f };
    glm::mat4 view_matrix { 1.0f };
    glm::mat4 projection_matrix { 1.0f };
    glm::vec3 camera_position { 0.0f, 0.0f, 0.0f };
  } scene_constants_;

  // Material constants structure matching shader layout
  struct MaterialConstants {
    glm::vec4 base_color { 1.0f, 1.0f, 1.0f, 1.0f }; // RGBA fallback color
    float metalness { 0.0f }; // Metalness scalar
    float roughness { 0.8f }; // Roughness scalar
    float normal_scale { 1.0f }; // Normal map scale
    float ambient_occlusion { 1.0f }; // AO scalar
    // Texture indices (bindless)
    uint32_t base_color_texture_index { 0 };
    uint32_t normal_texture_index { 0 };
    uint32_t metallic_texture_index { 0 };
    uint32_t roughness_texture_index { 0 };
    uint32_t ambient_occlusion_texture_index { 0 };
    uint32_t flags { 0 }; // Material flags
    float _pad0 { 0.0f }; // Padding for alignment
    float _pad1 { 0.0f }; // Padding for alignment
  };

  auto SetupCommandQueues() const -> void;
  auto SetupMainWindow() -> void;
  auto SetupSurface() -> void;
  auto SetupRenderer() -> void;
  auto SetupFramebuffers() -> void;
  auto SetupShaders() const -> void;

  auto RenderScene() -> co::Co<>;

  std::shared_ptr<Platform> platform_;
  std::weak_ptr<Graphics> gfx_weak_;
  std::weak_ptr<platform::Window> window_weak_;
  std::shared_ptr<graphics::Surface> surface_;
  std::shared_ptr<graphics::RenderController> render_controller_;
  std::shared_ptr<engine::Renderer> renderer_;
  StaticVector<std::shared_ptr<graphics::Framebuffer>, kFrameBufferCount>
    framebuffers_ {};
  engine::RenderContext context_ {};

  std::shared_ptr<graphics::Buffer> vertex_buffer_;
  std::shared_ptr<graphics::Buffer> index_buffer_;
  std::shared_ptr<graphics::Buffer> scene_constants_buffer_;
  std::shared_ptr<graphics::Buffer> indices_buffer_;
  std::shared_ptr<graphics::Buffer> material_constants_buffer_;

  // Helper methods for material support
  auto EnsureMaterialConstantsBuffer() -> void;
  auto UpdateMaterialConstantsBuffer(const MaterialConstants& constants) const
    -> void;
  auto ExtractMaterialConstants(const data::MaterialAsset& material) const
    -> MaterialConstants;

  // === Data-driven RenderItem/scene system members ===
  std::vector<engine::RenderItem> render_items_;

  co::Nursery* nursery_ { nullptr };
  auto EnsureBindlessIndexingBuffer() -> void;
  auto EnsureVertexBufferSrv() -> void;
  auto EnsureMeshDrawResources() -> void;
  auto EnsureSceneConstantsBuffer() -> void;
  auto UpdateSceneConstantsBuffer(const SceneConstants& constants) const
    -> void;

  uint32_t vertex_srv_shader_visible_index_ { 1 };
  bool recreate_indices_cbv_ { true };
};

} // namespace oxygen::examples
