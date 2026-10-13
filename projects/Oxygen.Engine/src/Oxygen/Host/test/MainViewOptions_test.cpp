//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Commands/Command.h>
#include <Oxygen/Commands/CommandRegistry.h>
#include <Oxygen/Console/Console.h>
#include <Oxygen/Core/Types/View.h>
#include <Oxygen/Host/MainViewOptions.h>
#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Vortex/CompositionView.h>
#include <Oxygen/Vortex/RenderMode.h>
#include <Oxygen/Vortex/ShaderDebugMode.h>

namespace {

using oxygen::View;
using oxygen::ViewId;
using oxygen::commands::ExecutionStatus;
using oxygen::console::Console;
using oxygen::host::MainViewOptions;
using oxygen::host::ShowFlag;
using oxygen::vortex::CompositionView;
using oxygen::vortex::RenderMode;
using oxygen::vortex::ShaderDebugMode;
using Mask = CompositionView::ViewFeatureMask;

auto MakeView(const ViewId id) -> CompositionView
{
  auto view = CompositionView::ForScene(id, View {}, {});
  view.with_atmosphere = true;
  view.with_height_fog = true;
  view.with_local_fog = true;
  return view;
}

auto SameRenderState(const CompositionView& lhs, const CompositionView& rhs)
  -> bool
{
  return lhs.render_settings.render_mode == rhs.render_settings.render_mode
    && lhs.render_settings.shader_debug_mode
    == rhs.render_settings.shader_debug_mode
    && lhs.feature_mask.bits == rhs.feature_mask.bits
    && lhs.feature_profile == rhs.feature_profile
    && lhs.with_atmosphere == rhs.with_atmosphere
    && lhs.with_height_fog == rhs.with_height_fog
    && lhs.with_local_fog == rhs.with_local_fog;
}

class MainViewOptionsTest : public testing::Test {
protected:
  void SetUp() override { MainViewOptions::RegisterConsoleBindings(console_); }

  auto Run(const std::string& line) -> ExecutionStatus
  {
    return console_.Execute(line).status;
  }

  [[nodiscard]] auto Options() const -> MainViewOptions
  {
    return MainViewOptions::FromConsole(console_);
  }

private:
  oxygen::commands::CommandRegistry commands_;
  Console console_ { oxygen::observer_ptr { &commands_ } };
};

//! Defaults leave a view exactly as it was built.
NOLINT_TEST_F(MainViewOptionsTest, DefaultsChangeNothing)
{
  const auto built = MakeView(ViewId { 1 });
  auto view = built;
  Options().ApplyTo(view);
  EXPECT_TRUE(SameRenderState(view, built));
}

//! Lit removes any render override the view carried.
NOLINT_TEST_F(MainViewOptionsTest, LitLeavesNoOverride)
{
  auto view = MakeView(ViewId { 1 });
  view.render_settings.render_mode = RenderMode::kWireframe;
  view.render_settings.shader_debug_mode = ShaderDebugMode::kRoughness;
  ASSERT_EQ(Run("host.view_mode lit"), ExecutionStatus::kOk);
  Options().ApplyTo(view);
  EXPECT_FALSE(view.render_settings.render_mode.has_value());
  EXPECT_FALSE(view.render_settings.shader_debug_mode.has_value());
}

//! A mode sets both settings from its catalog entry.
NOLINT_TEST_F(MainViewOptionsTest, ViewModeSetsRenderSettings)
{
  auto view = MakeView(ViewId { 1 });
  ASSERT_EQ(Run("host.view_mode wireframe"), ExecutionStatus::kOk);
  Options().ApplyTo(view);
  EXPECT_EQ(view.render_settings.render_mode, RenderMode::kWireframe);
  EXPECT_FALSE(view.render_settings.shader_debug_mode.has_value());

  ASSERT_EQ(Run("host.view_mode unlit"), ExecutionStatus::kOk);
  Options().ApplyTo(view);
  EXPECT_FALSE(view.render_settings.render_mode.has_value());
  EXPECT_EQ(
    view.render_settings.shader_debug_mode, ShaderDebugMode::kBaseColor);
}

//! Unknown and unsupported modes are rejected by the console.
NOLINT_TEST_F(MainViewOptionsTest, RejectsUnknownAndUnsupportedModes)
{
  EXPECT_EQ(
    Run("host.view_mode no-such-mode"), ExecutionStatus::kInvalidArguments);
  EXPECT_EQ(Run("host.view_mode light-culling-heat-map"),
    ExecutionStatus::kInvalidArguments);
  EXPECT_EQ(Options().view_mode, "lit");
}

//! Each show flag clears its own feature and nothing else.
NOLINT_TEST_F(MainViewOptionsTest, ShowFlagsOnlyRemoveTheirFeature)
{
  struct Case {
    const char* cvar;
    Mask::Bit bit;
  };
  constexpr auto kMaskCases = std::array {
    Case { .cvar = "host.show.shadows", .bit = Mask::kShadows },
    Case { .cvar = "host.show.environment", .bit = Mask::kEnvironment },
    Case { .cvar = "host.show.translucency", .bit = Mask::kTranslucency },
    Case { .cvar = "host.show.volumetrics", .bit = Mask::kVolumetrics },
    Case { .cvar = "host.show.grid", .bit = Mask::kGroundGrid },
  };
  for (const auto& [cvar, bit] : kMaskCases) {
    ASSERT_EQ(Run(std::string(cvar) + " false"), ExecutionStatus::kOk);
    auto view = MakeView(ViewId { 1 });
    Options().ApplyTo(view);
    EXPECT_FALSE(view.feature_mask.Has(bit)) << cvar;
    EXPECT_EQ(view.feature_mask.bits.get() | static_cast<uint64_t>(bit),
      static_cast<uint64_t>(Mask::kAll))
      << cvar;
    EXPECT_TRUE(
      view.with_atmosphere && view.with_height_fog && view.with_local_fog)
      << cvar;
    ASSERT_EQ(Run(std::string(cvar) + " true"), ExecutionStatus::kOk);
  }

  ASSERT_EQ(Run("host.show.height_fog false"), ExecutionStatus::kOk);
  auto view = MakeView(ViewId { 1 });
  Options().ApplyTo(view);
  EXPECT_FALSE(view.with_height_fog);
  EXPECT_TRUE(view.with_atmosphere);
  EXPECT_TRUE(view.with_local_fog);
  EXPECT_EQ(view.feature_mask.bits.get(), static_cast<uint64_t>(Mask::kAll));
}

//! A show flag never adds a feature the view did not ask for.
NOLINT_TEST_F(MainViewOptionsTest, ShowFlagsNeverAddFeatures)
{
  auto view = MakeView(ViewId { 1 });
  view.with_atmosphere = false;
  view.feature_mask.bits = CompositionView::ViewFeatureBits {
    Mask::kAll & ~static_cast<uint64_t>(Mask::kShadows),
  };
  const auto built = view;
  ASSERT_EQ(Run("host.show.atmosphere true"), ExecutionStatus::kOk);
  ASSERT_EQ(Run("host.show.shadows true"), ExecutionStatus::kOk);
  Options().ApplyTo(view);
  EXPECT_TRUE(SameRenderState(view, built));
}

//! Only the main view changes; the others are untouched.
NOLINT_TEST_F(MainViewOptionsTest, AppliesToTheMainViewOnly)
{
  ASSERT_EQ(Run("host.view_mode roughness"), ExecutionStatus::kOk);
  for (const auto& info : oxygen::host::kShowFlags) {
    ASSERT_EQ(Run(std::string(info.cvar) + " false"), ExecutionStatus::kOk);
  }
  const auto options = Options();
  EXPECT_FALSE(options.Shows(ShowFlag::kShadows));

  auto views = std::array {
    MakeView(ViewId { 1 }),
    MakeView(ViewId { 2 }),
    MakeView(ViewId { 3 }),
  };
  const auto built = views;
  options.ApplyToMain(views, ViewId { 2 });

  EXPECT_TRUE(SameRenderState(views[0], built[0]));
  EXPECT_FALSE(SameRenderState(views[1], built[1]));
  EXPECT_TRUE(SameRenderState(views[2], built[2]));

  auto unchanged = built;
  options.ApplyToMain(unchanged, std::nullopt);
  for (std::size_t i = 0; i < unchanged.size(); ++i) {
    EXPECT_TRUE(SameRenderState(unchanged.at(i), built.at(i)));
  }
}

} // namespace
