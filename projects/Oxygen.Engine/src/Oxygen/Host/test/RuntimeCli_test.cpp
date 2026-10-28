//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstdint>
#include <initializer_list>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

#include <Oxygen/Base/Types/Geometry.h>
#include <Oxygen/Clap/Cli.h>
#include <Oxygen/Clap/Command.h>
#include <Oxygen/Clap/CommandLineContext.h>
#include <Oxygen/Clap/Fluent/CommandBuilder.h>
#include <Oxygen/Config/GraphicsConfig.h>
#include <Oxygen/Console/CVar.h>
#include <Oxygen/Host/HostConfig.h>
#include <Oxygen/Host/RuntimeCli.h>
#include <Oxygen/Testing/GTest.h>

namespace {

using oxygen::FrameCaptureInitMode;
using oxygen::FrameCaptureProvider;
using oxygen::clap::CmdLineArgumentsError;
using oxygen::clap::Command;
using oxygen::clap::CommandBuilder;
using oxygen::clap::CommandLineContext;
using oxygen::console::CVarValue;
using oxygen::host::ApplyRuntimeCli;
using oxygen::host::BuildCli;
using oxygen::host::HostConfig;
using oxygen::host::MakeAdvancedCaptureOptions;
using oxygen::host::MakeCaptureOptions;
using oxygen::host::MakeGraphicsToolingOptions;
using oxygen::host::MakeRuntimeOptions;
using oxygen::host::ParseWindowResolution;
using oxygen::host::RuntimeCliError;
using oxygen::host::RuntimeCliState;
using oxygen::host::RuntimeOptionSet;

constexpr std::uint32_t kDefaultFps = 100U;

class RuntimeCliTest : public testing::Test {
protected:
  auto Parse(std::initializer_list<const char*> arguments,
    const RuntimeOptionSet selection = {}) -> CommandLineContext
  {
    const Command::Ptr command
      = CommandBuilder(Command::DEFAULT)
          .WithOptions(MakeRuntimeOptions(state_, selection))
          .WithOptions(MakeGraphicsToolingOptions(state_))
          .WithOptions(MakeCaptureOptions(state_))
          .WithOptions(MakeAdvancedCaptureOptions(state_), true);
    cli_ = BuildCli("host-test", "Runtime CLI test", command);
    argv_ = { "host-test" };
    argv_.insert(argv_.end(), arguments);
    return cli_->Parse(argv_);
  }

  auto ParseAndApply(std::initializer_list<const char*> arguments) -> void
  {
    const auto context = Parse(arguments);
    ApplyRuntimeCli(context, state_, config_);
  }

  [[nodiscard]] auto FindCVar(const std::string_view name) const
    -> std::optional<CVarValue>
  {
    for (const auto& entry : config_.startup_cvars.Entries()) {
      if (entry.name == name) {
        return entry.stamped_value.value;
      }
    }
    return std::nullopt;
  }

  [[nodiscard]] auto Config() const -> const HostConfig& { return config_; }
  auto MutableConfig() -> HostConfig& { return config_; }

private:
  RuntimeCliState state_ { .target_fps = kDefaultFps };
  HostConfig config_ {};
  std::unique_ptr<oxygen::clap::Cli> cli_;
  std::vector<const char*> argv_;
};

//! Options the user omits keep the application's defaults.
NOLINT_TEST_F(RuntimeCliTest, NoArgumentsKeepApplicationDefaults)
{
  ParseAndApply({});

  EXPECT_EQ(Config().engine.frame_count, 0U);
  EXPECT_EQ(Config().engine.target_fps, kDefaultFps);
  EXPECT_TRUE(Config().engine.graphics.enable_vsync);
  EXPECT_FALSE(Config().platform.headless);
  EXPECT_FALSE(Config().window.fullscreen);
  EXPECT_FALSE(Config().window.resolution.has_value());
  EXPECT_EQ(Config().engine.graphics.frame_capture.provider,
    FrameCaptureProvider::kNone);
  EXPECT_FALSE(FindCVar("ngin.target_fps").has_value());
  // A defaulted option is not passed, so persisted values still apply.
  EXPECT_FALSE(FindCVar("gfx.vsync").has_value());
}

//! Runtime options reach the engine, the window and the startup variables.
NOLINT_TEST_F(RuntimeCliTest, RuntimeOptionsFillEngineAndWindow)
{
  ParseAndApply({
    "--frames=5",
    "--fps=60",
    "--vsync=false",
    "--fullscreen",
    "--resolution=1920x1080",
  });

  EXPECT_EQ(Config().engine.frame_count, 5U);
  EXPECT_EQ(Config().engine.target_fps, 60U);
  EXPECT_FALSE(Config().engine.graphics.enable_vsync);
  EXPECT_TRUE(Config().window.fullscreen);
  const auto resolution
    = Config().window.resolution.value_or(oxygen::Extent<std::uint32_t> {});
  EXPECT_EQ(resolution.width, 1920U);
  EXPECT_EQ(resolution.height, 1080U);
  EXPECT_EQ(FindCVar("ngin.target_fps"), CVarValue { std::int64_t { 60 } });
  EXPECT_EQ(FindCVar("gfx.vsync"), CVarValue { false });
}

//! `--headless` selects a headless platform.
NOLINT_TEST_F(RuntimeCliTest, HeadlessSetsPlatformHeadless)
{
  ParseAndApply({ "--headless" });

  EXPECT_TRUE(Config().platform.headless);
}

//! A window resolution has no meaning without a window.
NOLINT_TEST_F(RuntimeCliTest, ResolutionWithHeadlessIsRejected)
{
  NOLINT_EXPECT_THROW(
    ParseAndApply({ "--headless", "--resolution=640x480" }), RuntimeCliError);
}

//! Resolutions need two positive dimensions separated by `x`.
NOLINT_TEST_F(RuntimeCliTest, MalformedResolutionIsRejected)
{
  for (const auto* value : { "1920", "0x1080", "1920x", "ax1080", "19x10y" }) {
    NOLINT_EXPECT_THROW((void)ParseWindowResolution(value), RuntimeCliError)
      << value;
  }
  EXPECT_EQ(ParseWindowResolution("640X480").height, 480U);
}

//! The debug layer and Aftermath cannot run together.
NOLINT_TEST_F(RuntimeCliTest, DebugLayerWithAftermathIsRejected)
{
  NOLINT_EXPECT_THROW(
    ParseAndApply({ "--debug-layer=true", "--aftermath=true" }),
    RuntimeCliError);
}

//! Graphics tooling switches reach the graphics configuration.
NOLINT_TEST_F(RuntimeCliTest, GraphicsToolingReachesGraphicsConfig)
{
  ParseAndApply({ "--debug-layer=false", "--aftermath=true" });

  EXPECT_FALSE(Config().engine.graphics.enable_debug_layer);
  EXPECT_TRUE(Config().engine.graphics.enable_aftermath);
}

//! Capture options produce the frame capture configuration.
NOLINT_TEST_F(RuntimeCliTest, CaptureOptionsBuildCaptureConfig)
{
  ParseAndApply({
    "--capture-provider=RenderDoc",
    "--capture-from-frame=3",
    "--capture-frame-count=2",
  });

  const auto& capture = Config().engine.graphics.frame_capture;
  EXPECT_EQ(capture.provider, FrameCaptureProvider::kRenderDoc);
  EXPECT_EQ(capture.init_mode, FrameCaptureInitMode::kAttachedOnly);
  EXPECT_EQ(capture.from_frame, 3U);
  EXPECT_EQ(capture.frame_count, 2U);
}

//! Capture needs a Direct3D12 run.
NOLINT_TEST_F(RuntimeCliTest, CaptureWithHeadlessIsRejected)
{
  NOLINT_EXPECT_THROW(
    ParseAndApply({ "--headless", "--capture-provider=pix" }), RuntimeCliError);
}

//! Unknown capture providers are rejected.
NOLINT_TEST_F(RuntimeCliTest, UnknownCaptureProviderIsRejected)
{
  NOLINT_EXPECT_THROW(
    ParseAndApply({ "--capture-provider=nsight" }), RuntimeCliError);
}

//! PIX cannot capture the startup frame: frame 0 has no PIX attachment yet.
NOLINT_TEST_F(RuntimeCliTest, PixCaptureFromFrameZeroIsRejected)
{
  NOLINT_EXPECT_THROW(ParseAndApply({ "--capture-provider=pix",
                        "--capture-load=search", "--capture-frame-count=1" }),
    RuntimeCliError);
}

//! A PIX capture of later frames is accepted.
NOLINT_TEST_F(RuntimeCliTest, PixCaptureFromPositiveFrameIsAccepted)
{
  ParseAndApply({
    "--capture-provider=pix",
    "--capture-load=search",
    "--capture-from-frame=1",
    "--capture-frame-count=1",
  });

  const auto& capture = Config().engine.graphics.frame_capture;
  EXPECT_EQ(capture.provider, FrameCaptureProvider::kPix);
  EXPECT_EQ(capture.init_mode, FrameCaptureInitMode::kSearchPath);
  EXPECT_EQ(capture.from_frame, 1U);
  EXPECT_EQ(capture.frame_count, 1U);
}

//! Without `--control` there is no control server.
NOLINT_TEST_F(RuntimeCliTest, ControlServerIsOffByDefault)
{
  ParseAndApply({});

  EXPECT_FALSE(Config().control.has_value());
}

//! `--control` alone takes an ephemeral port; with a value, that port.
NOLINT_TEST_F(RuntimeCliTest, ControlStartsTheServer)
{
  ParseAndApply({ "--control" });
  ASSERT_TRUE(Config().control.has_value());
  EXPECT_EQ(Config().control->port, 0U);

  ParseAndApply({ "--control=50513" });
  ASSERT_TRUE(Config().control.has_value());
  EXPECT_EQ(Config().control->port, 50513U);
}

//! The command line's port keeps the ownership rule the application set.
NOLINT_TEST_F(RuntimeCliTest, ControlPortKeepsTheOwnershipRule)
{
  MutableConfig().control = oxygen::control::ControlServerConfig {
    .port = 1U,
    .application = "Launcher",
    .exit_when_owner_leaves = true,
  };

  ParseAndApply({ "--control=50513" });

  ASSERT_TRUE(Config().control.has_value());
  EXPECT_EQ(Config().control->port, 50513U);
  EXPECT_EQ(Config().control->application, "Launcher");
  EXPECT_TRUE(Config().control->exit_when_owner_leaves);
}

//! `--control-name` labels the instance for control clients.
NOLINT_TEST_F(RuntimeCliTest, ControlNameLabelsTheInstance)
{
  ParseAndApply({ "--control", "--control-name=left" });

  ASSERT_TRUE(Config().control.has_value());
  EXPECT_EQ(Config().control->name, "left");
}

//! A label without a control server labels nothing.
NOLINT_TEST_F(RuntimeCliTest, ControlNameWithoutControlIsRejected)
{
  NOLINT_EXPECT_THROW(
    ParseAndApply({ "--control-name=left" }), RuntimeCliError);
}

//! A port beyond 16 bits is rejected.
NOLINT_TEST_F(RuntimeCliTest, ControlPortOutOfRangeIsRejected)
{
  NOLINT_EXPECT_THROW(ParseAndApply({ "--control=70000" }), RuntimeCliError);
}

//! An explicit capture library path is required with `--capture-load=path`.
NOLINT_TEST_F(RuntimeCliTest, CapturePathLoadWithoutLibraryIsRejected)
{
  NOLINT_EXPECT_THROW(
    ParseAndApply({ "--capture-provider=renderdoc", "--capture-load=path" }),
    RuntimeCliError);
}

//! Options left out of the selection are not accepted.
NOLINT_TEST_F(RuntimeCliTest, UnselectedRuntimeOptionIsUnknown)
{
  NOLINT_EXPECT_THROW(
    (void)Parse({ "--headless" }, RuntimeOptionSet { .headless = false }),
    CmdLineArgumentsError);
}

} // namespace
