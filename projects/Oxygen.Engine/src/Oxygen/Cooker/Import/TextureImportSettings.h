//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace oxygen::content::import {

struct TextureSourceMapping {
  std::string file;
  uint16_t layer = 0;
  uint16_t mip = 0;
  uint16_t slice = 0;
};

struct TextureImportSettings {
  std::string source_path;
  std::vector<TextureSourceMapping> sources;
  std::string cooked_root;
  std::string job_name;
  std::string report_path;
  bool verbose = false;
  std::string intent;
  std::string color_space;
  std::string output_format;
  std::string data_format;
  std::string preset;
  std::string mip_policy;
  std::string mip_filter;
  std::string mip_filter_space;
  std::string bc7_quality;
  std::string packing_policy;
  std::string cube_layout;
  std::string hdr_handling;
  float exposure_ev = 0.0f;
  uint32_t max_mip_levels = 0;
  uint32_t cube_face_size = 0;
  bool flip_y = false;
  bool force_rgba = true;
  bool flip_normal_green = false;
  bool renormalize_normals = true;
  bool bake_hdr_to_ldr = false;
  bool cubemap = false;
  bool equirect_to_cube = false;
  bool with_content_hashing = true;
};

} // namespace oxygen::content::import
