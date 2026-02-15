//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// This style is inspired/originally by Adobe ImGui Spectrum style.
// https://github.com/adobe/imgui

#include <imgui.h>
#include <span>

#include <Oxygen/ImGui/Styles/Spectrum.h>

// NOLINTBEGIN(*-avoid-c-arrays,*-array-to-pointer-decay,*-magic-numbers)

namespace oxygen::imgui::spectrum {

extern const unsigned int SourceSansProRegular_compressed_size = 149392;
extern const unsigned int SourceSansProRegular_compressed_data[];

auto LoadFont(ImFontAtlas& font_atlas, float size) -> ImFont*
{
  ImFont* font = font_atlas.AddFontFromMemoryCompressedTTF(
    SourceSansProRegular_compressed_data, SourceSansProRegular_compressed_size,
    size);
  IM_ASSERT(font != nullptr);
  return font;
}

void StyleColorsSpectrum(ImGuiStyle& style)
{
  style.FrameRounding = 2.0F;
  style.FrameBorderSize = 2.0F;
  style.GrabRounding = 4.0F;

  std::span colors { style.Colors };
  // clang-format off
  colors[ImGuiCol_Text] = ImGui::ColorConvertU32ToFloat4(kGray800); // text on hovered controls is gray900
  colors[ImGuiCol_TextDisabled] = ImGui::ColorConvertU32ToFloat4(kGray500);
  colors[ImGuiCol_WindowBg] = ImGui::ColorConvertU32ToFloat4(kGray100);
  colors[ImGuiCol_ChildBg] = ImVec4(0.00F, 0.00F, 0.00F, 0.00F);
  colors[ImGuiCol_PopupBg] = ImGui::ColorConvertU32ToFloat4(kGray50); // not sure about this. Note: applies to tooltips too.
  colors[ImGuiCol_Border] = ImGui::ColorConvertU32ToFloat4(kGray300);
  colors[ImGuiCol_BorderShadow] = ImGui::ColorConvertU32ToFloat4(Static::kNone); // We don't want shadows. Ever.
  colors[ImGuiCol_FrameBg] = ImGui::ColorConvertU32ToFloat4(kGray75); // this isnt right, spectrum does not do this, but it's a good fallback
  colors[ImGuiCol_FrameBgHovered] = ImGui::ColorConvertU32ToFloat4(kGray50);
  colors[ImGuiCol_FrameBgActive] = ImGui::ColorConvertU32ToFloat4(kGray200);
  colors[ImGuiCol_TitleBg] = ImGui::ColorConvertU32ToFloat4(kGray300); // those titlebar values are totally made up, spectrum does not have this.
  colors[ImGuiCol_TitleBgActive] = ImGui::ColorConvertU32ToFloat4(kGray200);
  colors[ImGuiCol_TitleBgCollapsed] = ImGui::ColorConvertU32ToFloat4(kGray400);
  colors[ImGuiCol_MenuBarBg] = ImGui::ColorConvertU32ToFloat4(kGray100);
  colors[ImGuiCol_ScrollbarBg] = ImGui::ColorConvertU32ToFloat4(kGray100); // same as regular background
  colors[ImGuiCol_ScrollbarGrab] = ImGui::ColorConvertU32ToFloat4(kGray400);
  colors[ImGuiCol_ScrollbarGrabHovered] = ImGui::ColorConvertU32ToFloat4(kGray600);
  colors[ImGuiCol_ScrollbarGrabActive] = ImGui::ColorConvertU32ToFloat4(kGray700);
  colors[ImGuiCol_CheckMark] = ImGui::ColorConvertU32ToFloat4(kBlue500);
  colors[ImGuiCol_SliderGrab] = ImGui::ColorConvertU32ToFloat4(kGray700);
  colors[ImGuiCol_SliderGrabActive] = ImGui::ColorConvertU32ToFloat4(kGray800);
  colors[ImGuiCol_Button] = ImGui::ColorConvertU32ToFloat4(kGray75); // match default button to Spectrum's 'Action Button'.
  colors[ImGuiCol_ButtonHovered] = ImGui::ColorConvertU32ToFloat4(kGray50);
  colors[ImGuiCol_ButtonActive] = ImGui::ColorConvertU32ToFloat4(kGray200);
  colors[ImGuiCol_Header] = ImGui::ColorConvertU32ToFloat4(kBlue400);
  colors[ImGuiCol_HeaderHovered] = ImGui::ColorConvertU32ToFloat4(kBlue500);
  colors[ImGuiCol_HeaderActive] = ImGui::ColorConvertU32ToFloat4(kBlue600);
  colors[ImGuiCol_Separator] = ImGui::ColorConvertU32ToFloat4(kGray400);
  colors[ImGuiCol_SeparatorHovered] = ImGui::ColorConvertU32ToFloat4(kGray600);
  colors[ImGuiCol_SeparatorActive] = ImGui::ColorConvertU32ToFloat4(kGray700);
  colors[ImGuiCol_ResizeGrip] = ImGui::ColorConvertU32ToFloat4(kGray400);
  colors[ImGuiCol_ResizeGripHovered] = ImGui::ColorConvertU32ToFloat4(kGray600);
  colors[ImGuiCol_ResizeGripActive] = ImGui::ColorConvertU32ToFloat4(kGray700);
  colors[ImGuiCol_PlotLines] = ImGui::ColorConvertU32ToFloat4(kBlue400);
  colors[ImGuiCol_PlotLinesHovered] = ImGui::ColorConvertU32ToFloat4(kBlue600);
  colors[ImGuiCol_PlotHistogram] = ImGui::ColorConvertU32ToFloat4(kBlue400);
  colors[ImGuiCol_PlotHistogramHovered] = ImGui::ColorConvertU32ToFloat4(kBlue600);
  colors[ImGuiCol_TextSelectedBg] = ImGui::ColorConvertU32ToFloat4((kBlue400 & 0x00FFFFFF) | 0x33000000);
  colors[ImGuiCol_DragDropTarget] = ImVec4(1.00F, 1.00F, 0.00F, 0.90F);
  colors[ImGuiCol_NavCursor] = ImGui::ColorConvertU32ToFloat4((kGray900 & 0x00FFFFFF) | 0x0A000000);
  colors[ImGuiCol_NavWindowingHighlight] = ImVec4(1.00F, 1.00F, 1.00F, 0.70F);
  colors[ImGuiCol_NavWindowingDimBg] = ImVec4(0.80F, 0.80F, 0.80F, 0.20F);
  colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.20F, 0.20F, 0.20F, 0.35F);
  colors[ImGuiCol_CheckMark] = ImGui::ColorConvertU32ToFloat4(kGray400);
  colors[ImGuiCol_Tab] = ImGui::ColorConvertU32ToFloat4(kGray300);
  colors[ImGuiCol_TabSelected] = ImGui::ColorConvertU32ToFloat4(kBlue500);
  colors[ImGuiCol_TabHovered] = ImGui::ColorConvertU32ToFloat4(kBlue700);
  colors[ImGuiCol_TabDimmed] = ImGui::ColorConvertU32ToFloat4(kGray400);
  colors[ImGuiCol_TabDimmedSelected] = ImGui::ColorConvertU32ToFloat4(kBlue700);
  // clang-format on
}

} // namespace oxygen::imgui::spectrum

// NOLINTEND(*-avoid-c-arrays,*-array-to-pointer-decay,*-magic-numbers)
