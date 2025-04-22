//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Core/Engine.h>

#include <algorithm>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/TimeUtils.h>
#include <Oxygen/Core/Version.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/Renderer.h>
#include <Oxygen/ImGui/ImguiModule.h>
#include <Oxygen/Platform/Platform.h>

using oxygen::Engine;

Engine::Engine(PlatformPtr platform, GraphicsPtr graphics, Properties props)
    : platform_(std::move(platform))
    , graphics_(std::move(graphics))
    , props_(std::move(props))
{
    if (graphics_.expired())
        DLOG_F(INFO, "Engine created without a graphics backend");
    else
        DLOG_F(INFO, "Engine created");
}

Engine::~Engine() noexcept
{
    DCHECK_F(!is_running_);
    DLOG_F(INFO, "Engine destroyed");
}

auto Engine::GetPlatform() const -> Platform& { return *platform_; }

auto Engine::Name() -> const std::string&
{
    static const std::string kName { "Oxygen" };
    return kName;
}

auto Engine::Version() -> uint32_t
{
    constexpr uint32_t kBitsPatch { 12 };
    constexpr uint32_t kBitsMinor { 10 };
    return (static_cast<uint32_t>(version::Major())
               << (kBitsPatch + kBitsMinor))
        | ((static_cast<uint32_t>(version::Minor())) << kBitsPatch) | (static_cast<uint32_t>(version::Patch()));
}

auto Engine::GetImGuiRenderInterface() const -> imgui::ImGuiRenderInterface
{
    if (!imgui_module_) {
        throw std::runtime_error("ImGui module is not enabled.");
    }

    return imgui_module_->GetRenderInterface();
}

void Engine::AttachModule(const ModulePtr& module, const uint32_t layer)
{
    LOG_SCOPE_FUNCTION(INFO);
    DLOG_F(INFO, "module name: {}", module->Name());
    DLOG_F(1, "module layer: {}", layer);
    if (std::ranges::find_if(modules_, [&module](const auto& module_ctx) {
            return module_ctx.module == module;
        })
        != modules_.end()) {
        throw std::invalid_argument("The module is already attached.");
    }

    modules_.push_back(ModuleContext { .module = module, .layer = layer });
    ReorderLayers();

    if (is_running_) {
        module->Initialize(graphics_.lock().get());
    }
}

void Engine::DetachModule(const ModulePtr& module)
{
    LOG_SCOPE_FUNCTION(INFO);
    DLOG_F(INFO, "module name: {}", module->Name());
    if (const auto it = std::ranges::find_if(modules_,
            [&module](const auto& module_ctx) {
                return module_ctx.module == module;
            });
        it != modules_.end()) {
        modules_.erase(it);
    }
    if (is_running_) {
        module->Shutdown();
    }
}

void Engine::ReorderLayers()
{
    DLOG_F(1, "reordering ({}) modules by layer", modules_.size());
    modules_.sort([](const ModuleContext& a, const ModuleContext& b) {
        return a.layer < b.layer;
    });
}

void Engine::InitializeModules()
{
    const auto gfx = graphics_.lock();
    // We may be running without graphics
    std::ranges::for_each(
        modules_, [&](auto& module) { module.module->Initialize(gfx.get()); });
}

void Engine::ShutdownModules() noexcept
{
    std::ranges::for_each(modules_,
        [](auto& module) { module.module->Shutdown(); });
}

void Engine::InitializeImGui()
{
    const auto gfx = graphics_.lock();
    if (gfx && props_.enable_imgui_layer) {
        // Initialize ImGui if required
        imgui_module_ = gfx->CreateImGuiModule(shared_from_this(), props_.main_window_id);
        imgui_module_->Initialize(gfx.get());
    }
}

void Engine::ShutdownImGui() noexcept
{
    if (imgui_module_) {
        imgui_module_->Shutdown();
        imgui_module_.reset();
    }
}

void Engine::Stop()
{
    if (!is_running_ || is_stop_requested_.load())
        return;

    is_stop_requested_.store(true);
    LOG_F(INFO, "Engine stop requested");
}

auto Engine::Run() -> void
{
    is_running_ = true;
    bool continue_running { true };
    {
        LOG_SCOPE_F(INFO, "Engine pre-Run init");
        InitializeModules();
        InitializeImGui();

        // Start the master clock
        engine_clock_.Reset();

        // https://gafferongames.com/post/fix_your_timestep/
        std::ranges::for_each(modules_,
            [](auto& module) { module.frame_time.Reset(); });
    }
    while (continue_running && !is_stop_requested_.load()) {
        // Poll for platform events
        // TODO: FIXME upgrade Engine to async
        // auto event = GetPlatform().PollEvent();

        // Process Input Events with ImGui
#if 0
        if (event && imgui_module_) {
            imgui_module_->ProcessInput(*event);
        }
#endif
        // Run the modules
        std::ranges::for_each(
            // modules_, [this, &continue_running, &event](auto& module) {
            modules_, [this, &continue_running](auto& module) {
                auto& the_module = module.module;
                DCHECK_NOTNULL_F(the_module);

#if 0
                // Inputs
                if (event) {
                    the_module->ProcessInput(*event);
                }
#endif

                // Note that we may be running renderer-less, which means the renderer
                // is null, which is fine.
                const auto gfx = graphics_.lock();

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
                    if (gfx) {
                        the_module->Render(gfx.get());
                    }
                    module.fps.Update();

                    // Log FPS and UPS once every second
                    if (module.log_timer.ElapsedTime() >= std::chrono::seconds(1)) {
                        LOG_F(INFO, "FPS: {} UPS: {}", module.fps.Value(),
                            module.ups.Value());
                        module.log_timer = {};
                    }
                }
            });
    }
    LOG_F(INFO, "Engine stopped");
    {
        LOG_SCOPE_F(INFO, "Engine post-Run shutdown");

        is_stop_requested_ = false;
        is_running_ = false;

        ShutdownImGui();
        ShutdownModules();
    }
}
