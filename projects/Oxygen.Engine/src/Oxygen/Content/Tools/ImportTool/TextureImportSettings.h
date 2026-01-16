//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <string>

namespace oxygen::content::import::tool {

struct TextureImportSettings {
  std::string source_path;
  std::string cooked_root;
  std::string job_name;
  std::string report_path;
  std::string intent;
  std::string color_space;
  std::string output_format;
  std::string data_format;
  std::string mip_policy;
  std::string mip_filter;
  std::string bc7_quality;
  std::string packing_policy;
  std::string cube_layout;
  uint32_t max_mip_levels = 1;
  uint32_t cube_face_size = 0;
  bool flip_y = false;
  bool force_rgba = true;
  bool cubemap = false;
  bool equirect_to_cube = false;
  bool verbose = false;
};

} // namespace oxygen::content::import::tool
