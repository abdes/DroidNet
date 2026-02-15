//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <imgui.h>

#include <Oxygen/ImGui/api_export.h>

// This style is inspired/originally by Adobe ImGui Spectrum style.
// https://github.com/adobe/imgui

/*
Color definitions in ImGui are a good starting point, but do not cover all the
intricacies of Spectrum's possible colors in controls and widgets.

One big difference is that ImGui communicates widget activity (hover, pressed)
with their background, while spectrum uses a mix of background and border, with
border being the most common choice.

Because of this, we reference extra colors in spectrum from imgui.cpp and
imgui_widgets.cpp directly, and to make that work, we need to have them defined
at here at compile time.
*/

/// Pick one, or have one defined already.
#if !defined(OXYGEN_IMGUI_USE_LIGHT_THEME)                                     \
  && !defined(OXYGEN_IMGUI_USE_DARK_THEME)
#  define OXYGEN_IMGUI_USE_LIGHT_THEME
// #define OXYGEN_IMGUI_USE_DARK_THEME
#endif

namespace oxygen::imgui::spectrum {

constexpr float kDefaultFontSize = 16.0F;

// Add SourceSansProRegular to the provided font atlas.
// Caller is responsible for assigning ImGuiIO::FontDefault if desired.
OXGN_IMGUI_API auto LoadFont(
  ImFontAtlas& font_atlas, float size = kDefaultFontSize) -> ImFont*;

// Sets the ImGui style to Spectrum
OXGN_IMGUI_API void StyleColorsSpectrum(ImGuiStyle& style);

constexpr auto Color(unsigned int c) -> unsigned int
{
  // add alpha.
  // also swap red and blue channel for some reason.
  // todo: figure out why, and fix it.
  const uint16_t a = 0xFF;
  const uint16_t r = (c >> 16) & 0xFF;
  const uint16_t g = (c >> 8) & 0xFF;
  const uint16_t b = (c >> 0) & 0xFF;
  // NOLINTNEXTLINE(*-magic-numbers)
  return (a << 24) | (r << 0) | (g << 8) | (b << 16);
}
// all colors are from http://spectrum.corp.adobe.com/color.html

constexpr auto color_alpha(unsigned int alpha, unsigned int c) -> unsigned int
{
  // NOLINTNEXTLINE(*-magic-numbers)
  return ((alpha & 0xFF) << 24) | (c & 0x00FFFFFF);
}

namespace Static { // static colors
  constexpr unsigned int kNone = 0x00000000; // transparent
  constexpr unsigned int kWhite = Color(0xFFFFFF);
  constexpr unsigned int kBlack = Color(0x000000);
  constexpr unsigned int kGray200 = Color(0xF4F4F4);
  constexpr unsigned int kGray300 = Color(0xEAEAEA);
  constexpr unsigned int kGray400 = Color(0xD3D3D3);
  constexpr unsigned int kGray500 = Color(0xBCBCBC);
  constexpr unsigned int kGray600 = Color(0x959595);
  constexpr unsigned int kGray700 = Color(0x767676);
  constexpr unsigned int kGray800 = Color(0x505050);
  constexpr unsigned int kGray900 = Color(0x323232);
  constexpr unsigned int kBlue400 = Color(0x378EF0);
  constexpr unsigned int kBlue500 = Color(0x2680EB);
  constexpr unsigned int kBlue600 = Color(0x1473E6);
  constexpr unsigned int kBlue700 = Color(0x0D66D0);
  constexpr unsigned int kRed400 = Color(0xEC5B62);
  constexpr unsigned int kRed500 = Color(0xE34850);
  constexpr unsigned int kRed600 = Color(0xD7373F);
  constexpr unsigned int kRed700 = Color(0xC9252D);
  constexpr unsigned int kOrange400 = Color(0xF29423);
  constexpr unsigned int kOrange500 = Color(0xE68619);
  constexpr unsigned int kOrange600 = Color(0xDA7B11);
  constexpr unsigned int kOrange700 = Color(0xCB6F10);
  constexpr unsigned int kGreen400 = Color(0x33AB84);
  constexpr unsigned int kGreen500 = Color(0x2D9D78);
  constexpr unsigned int kGreen600 = Color(0x268E6C);
  constexpr unsigned int kGreen700 = Color(0x12805C);
} // namespace Static

#ifdef OXYGEN_IMGUI_USE_LIGHT_THEME
constexpr unsigned int kGray50 = Static::kWhite;
constexpr unsigned int kGray75 = Color(0xFAFAFA);
constexpr unsigned int kGray100 = Color(0xF5F5F5);
constexpr unsigned int kGray200 = Static::kGray300;
constexpr unsigned int kGray300 = Color(0xE1E1E1);
constexpr unsigned int kGray400 = Color(0xCACACA);
constexpr unsigned int kGray500 = Color(0xB3B3B3);
constexpr unsigned int kGray600 = Color(0x8E8E8E);
constexpr unsigned int kGray700 = Color(0x707070);
constexpr unsigned int kGray800 = Color(0x4B4B4B);
constexpr unsigned int kGray900 = Color(0x2C2C2C);
constexpr unsigned int kBlue400 = Static::kBlue500;
constexpr unsigned int kBlue500 = Static::kBlue600;
constexpr unsigned int kBlue600 = Static::kBlue700;
constexpr unsigned int kBlue700 = Color(0x095ABA);
constexpr unsigned int kRed400 = Static::kRed500;
constexpr unsigned int kRed500 = Static::kRed600;
constexpr unsigned int kRed600 = Static::kRed700;
constexpr unsigned int kRed700 = Color(0xBB121A);
constexpr unsigned int kOrange400 = Static::kOrange500;
constexpr unsigned int kOrange500 = Static::kOrange600;
constexpr unsigned int kOrange600 = Static::kOrange700;
constexpr unsigned int kOrange700 = Color(0xBD640D);
constexpr unsigned int kGreen400 = Static::kGreen500;
constexpr unsigned int kGreen500 = Static::kGreen600;
constexpr unsigned int kGreen600 = Static::kGreen700;
constexpr unsigned int kGreen700 = Color(0x107154);
constexpr unsigned int kIndigo400 = Color(0x6767EC);
constexpr unsigned int kIndigo500 = Color(0x5C5CE0);
constexpr unsigned int kIndigo600 = Color(0x5151D3);
constexpr unsigned int kIndigo700 = Color(0x4646C6);
constexpr unsigned int kCelery400 = Color(0x44B556);
constexpr unsigned int kCelery500 = Color(0x3DA74E);
constexpr unsigned int kCelery600 = Color(0x379947);
constexpr unsigned int kCelery700 = Color(0x318B40);
constexpr unsigned int kMagenta400 = Color(0xD83790);
constexpr unsigned int kMagenta500 = Color(0xCE2783);
constexpr unsigned int kMagenta600 = Color(0xBC1C74);
constexpr unsigned int kMagenta700 = Color(0xAE0E66);
constexpr unsigned int kYellow400 = Color(0xDFBF00);
constexpr unsigned int kYellow500 = Color(0xD2B200);
constexpr unsigned int kYellow600 = Color(0xC4A600);
constexpr unsigned int kYellow700 = Color(0xB79900);
constexpr unsigned int kFuchsia400 = Color(0xC038CC);
constexpr unsigned int kFuchsia500 = Color(0xB130BD);
constexpr unsigned int kFuchsia600 = Color(0xA228AD);
constexpr unsigned int kFuchsia700 = Color(0x93219E);
constexpr unsigned int kSeafoam400 = Color(0x1B959A);
constexpr unsigned int kSeafoam500 = Color(0x16878C);
constexpr unsigned int kSeafoam600 = Color(0x0F797D);
constexpr unsigned int kSeafoam700 = Color(0x096C6F);
constexpr unsigned int kChartreuse400 = Color(0x85D044);
constexpr unsigned int kChartreuse500 = Color(0x7CC33F);
constexpr unsigned int kChartreuse600 = Color(0x73B53A);
constexpr unsigned int kChartreuse700 = Color(0x6AA834);
constexpr unsigned int kPurple400 = Color(0x9256D9);
constexpr unsigned int kPurple500 = Color(0x864CCC);
constexpr unsigned int kPurple600 = Color(0x7A42BF);
constexpr unsigned int kPurple700 = Color(0x6F38B1);
#endif
#ifdef OXYGEN_IMGUI_USE_DARK_THEME
constexpr unsigned int kGray50 = Color(0x252525);
constexpr unsigned int kGray75 = Color(0x2F2F2F);
constexpr unsigned int kGray100 = Static::kGray900;
constexpr unsigned int kGray200 = Color(0x393939);
constexpr unsigned int kGray300 = Color(0x3E3E3E);
constexpr unsigned int kGray400 = Color(0x4D4D4D);
constexpr unsigned int kGray500 = Color(0x5C5C5C);
constexpr unsigned int kGray600 = Color(0x7B7B7B);
constexpr unsigned int kGray700 = Color(0x999999);
constexpr unsigned int kGray800 = Color(0xCDCDCD);
constexpr unsigned int kGray900 = Static::kWhite;
constexpr unsigned int kBlue400 = Static::kBlue500;
constexpr unsigned int kBlue500 = Static::kBlue400;
constexpr unsigned int kBlue600 = Color(0x4B9CF5);
constexpr unsigned int kBlue700 = Color(0x5AA9FA);
constexpr unsigned int kRed400 = Static::kRed500;
constexpr unsigned int kRed500 = Static::kRed400;
constexpr unsigned int kRed600 = Color(0xF76D74);
constexpr unsigned int kRed700 = Color(0xFF7B82);
constexpr unsigned int kOrange400 = Static::kOrange500;
constexpr unsigned int kOrange500 = Static::kOrange400;
constexpr unsigned int kOrange600 = Color(0xF9A43F);
constexpr unsigned int kOrange700 = Color(0xFFB55B);
constexpr unsigned int kGreen400 = Static::kGreen500;
constexpr unsigned int kGreen500 = Static::kGreen400;
constexpr unsigned int kGreen600 = Color(0x39B990);
constexpr unsigned int kGreen700 = Color(0x3FC89C);
constexpr unsigned int kIndigo400 = Color(0x6767EC);
constexpr unsigned int kIndigo500 = Color(0x7575F1);
constexpr unsigned int kIndigo600 = Color(0x8282F6);
constexpr unsigned int kIndigo700 = Color(0x9090FA);
constexpr unsigned int kCelery400 = Color(0x44B556);
constexpr unsigned int kCelery500 = Color(0x4BC35F);
constexpr unsigned int kCelery600 = Color(0x51D267);
constexpr unsigned int kCelery700 = Color(0x58E06F);
constexpr unsigned int kMagenta400 = Color(0xD83790);
constexpr unsigned int kMagenta500 = Color(0xE2499D);
constexpr unsigned int kMagenta600 = Color(0xEC5AAA);
constexpr unsigned int kMagenta700 = Color(0xF56BB7);
constexpr unsigned int kYellow400 = Color(0xDFBF00);
constexpr unsigned int kYellow500 = Color(0xEDCC00);
constexpr unsigned int kYellow600 = Color(0xFAD900);
constexpr unsigned int kYellow700 = Color(0xFFE22E);
constexpr unsigned int kFuchsia400 = Color(0xC038CC);
constexpr unsigned int kFuchsia500 = Color(0xCF3EDC);
constexpr unsigned int kFuchsia600 = Color(0xD951E5);
constexpr unsigned int kFuchsia700 = Color(0xE366EF);
constexpr unsigned int kSeafoam400 = Color(0x1B959A);
constexpr unsigned int kSeafoam500 = Color(0x20A3A8);
constexpr unsigned int kSeafoam600 = Color(0x23B2B8);
constexpr unsigned int kSeafoam700 = Color(0x26C0C7);
constexpr unsigned int kChartreuse400 = Color(0x85D044);
constexpr unsigned int kChartreuse500 = Color(0x8EDE49);
constexpr unsigned int kChartreuse600 = Color(0x9BEC54);
constexpr unsigned int kChartreuse700 = Color(0xA3F858);
constexpr unsigned int kPurple400 = Color(0x9256D9);
constexpr unsigned int kPurple500 = Color(0x9D64E1);
constexpr unsigned int kPurple600 = Color(0xA873E9);
constexpr unsigned int kPurple700 = Color(0xB483F0);
#endif

} // namespace oxygen::imgui::spectrum
