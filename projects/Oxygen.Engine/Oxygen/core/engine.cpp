//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "Oxygen/Core/Engine.h"

#include <algorithm>

#include "Oxygen/Base/Time.h"
#include "Oxygen/Base/logging.h"
#include "Oxygen/Core/Version.h"
#include "Oxygen/Graphics/Common/Graphics.h"
#include "Oxygen/Graphics/Common/Renderer.h"
#include "Oxygen/ImGui/ImguiModule.h"
#include "Oxygen/Platform/Common/Platform.h"

using oxygen::Engine;

auto Engine::GetPlatform() const -> Platform&
{
  return *platform_;
}

auto Engine::Name() -> const std::string&
{
  static const std::string kName { "Oxygen" };
  return kName;
}

auto Engine::Version() -> uint32_t
{
  constexpr uint32_t kBitsPatch { 12 };
  constexpr uint32_t kBitsMinor { 10 };
  return (static_cast<uint32_t>(version::Major()) << (kBitsPatch + kBitsMinor))
    | ((static_cast<uint32_t>(version::Minor())) << kBitsPatch)
    | (static_cast<uint32_t>(version::Patch()));
}

auto Engine::GetImGuiRenderInterface() const -> imgui::ImGuiRenderInterface
{
  if (!imgui_module_) {
    throw std::runtime_error("ImGui module is not enabled.");
  }

  return imgui_module_->GetRenderInterface();
}

void Engine::OnInitialize()
{
  const auto gfx = graphics_.lock();
  if (!gfx)
    return;

  InitializeModules();

  if (!gfx->IsWithoutRenderer() && props_.enable_imgui_layer) {
    // Initialize ImGui if required
    DCHECK_NOTNULL_F(gfx->GetRenderer());
    imgui_module_ = gfx->CreateImGuiModule(shared_from_this(), props_.main_window_id);
    imgui_module_->Initialize(gfx.get());
  }
}

void Engine::OnShutdown()
{
  if (imgui_module_) {
    imgui_module_->Shutdown();
    imgui_module_.reset();
  }

  ShutdownModules();
}

void Engine::AttachModule(const ModulePtr& module, const uint32_t priority)
{
  DCHECK_F(!IsInitialized());

  if (std::ranges::find_if(
        modules_,
        [&module](const auto& module_ctx) {
          return module_ctx.module == module;
        })
    != modules_.end()) {
    throw std::invalid_argument("The module is already attached.");
  }

  modules_.push_back(ModuleContext { .module = module, .layer = priority });
  SortModulesByPriority();
}

void Engine::DetachModule(const ModulePtr& module)
{
  if (const auto it = std::ranges::find_if(
        modules_,
        [&module](const auto& module_ctx) {
          return module_ctx.module == module;
        });
    it != modules_.end()) {
    modules_.erase(it);
  }
}

void Engine::SortModulesByPriority()
{
  modules_.sort(
    [](const ModuleContext& a, const ModuleContext& b) {
      return a.layer < b.layer;
    });
}

void Engine::InitializeModules()
{
  const auto gfx = graphics_.lock();
  if (!gfx)
    return;
  std::ranges::for_each(modules_,
    [&](auto& module) {
      module.module->Initialize(gfx.get());
    });
}

void Engine::ShutdownModules()
{
  std::ranges::for_each(modules_, [](auto& module) { module.module->Shutdown(); });
}

auto Engine::Run() -> void
{
  DCHECK_F(IsInitialized(), "engine must be initialized before Run() is called");

  bool continue_running { true };

  // Listen for the last window closed event
  auto last_window_closed_con = GetPlatform().OnLastWindowClosed().connect(
    [&continue_running]() { continue_running = false; });

  // Start the master clock
  engine_clock_.Reset();

  // https://gafferongames.com/post/fix_your_timestep/
  std::ranges::for_each(
    modules_,
    [](auto& module) {
      module.frame_time.Reset();
    });

  while (continue_running) {
    // Poll for platform events
    auto event = GetPlatform().PollEvent();

    // Process Input Events with ImGui
    if (event && imgui_module_) {
      imgui_module_->ProcessInput(*event);
    }
    // Run the modules
    std::ranges::for_each(
      modules_,
      [this, &continue_running, &event](auto& module) {
        auto& the_module = module.module;
        DCHECK_NOTNULL_F(the_module);

        // Inputs
        if (event) {
          the_module->ProcessInput(*event);
        }

        // Note that we may be running renderer-less, which means the renderer
        // is null, which is fine.
        auto graphics = graphics_.lock();
        DCHECK_NOTNULL_F(graphics);

        if (continue_running) {
          module.frame_time.Update();
          auto delta = module.frame_time.Delta();

          // Fixed updates
          if (delta > props_.max_fixed_update_duration) {
            delta = props_.max_fixed_update_duration;
          }
          module.fixed_accumulator += module.frame_time.Delta();
          while (module.fixed_accumulator >= module.fixed_interval) {
            the_module->FixedUpdate(
              // module.time_since_start.ElapsedTime(),
              // module.fixed_interval
            );
            module.fixed_accumulator -= module.fixed_interval;
            module.ups.Update();
          }
          // TODO: Interpolate the remaining time in the
          // accumulator const float alpha =
          //    static_cast<float>(module.fixed_accumulator.count()) /
          //    static_cast<float>(module.fixed_interval.count());
          // the_module->FixedUpdate(/*alpha*/);

          // Per frame updates / render
          the_module->Update(module.frame_time.Delta());
          the_module->Render(graphics.get());
          module.fps.Update();

          // Log FPS and UPS once every second
          if (module.log_timer.ElapsedTime() >= std::chrono::seconds(1)) {
            LOG_F(INFO, "FPS: {} UPS: {}", module.fps.Value(), module.ups.Value());
            module.log_timer = {};
          }
        }
      });
  }
  LOG_F(INFO, "Engine stopped.");

  // TODO: we may want to have this become the responsibility of the application main
  // Stop listening for the last window closed event
  last_window_closed_con.disconnect();
}
