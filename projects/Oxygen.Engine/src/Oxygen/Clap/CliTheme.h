//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <fmt/color.h>

namespace oxygen::clap {

struct CliTheme {
  fmt::text_style section_header;
  fmt::text_style command_name;
  fmt::text_style option_flag;
  fmt::text_style placeholder;
  fmt::text_style note;
  fmt::text_style default_value;
  fmt::text_style example;
  fmt::text_style reset;

  static const CliTheme& Plain()
  {
    static const CliTheme theme = {
      .section_header = fmt::text_style(),
      .command_name = fmt::text_style(),
      .option_flag = fmt::text_style(),
      .placeholder = fmt::text_style(),
      .note = fmt::text_style(),
      .default_value = fmt::text_style(),
      .example = fmt::text_style(),
      .reset = fmt::text_style(),
    };
    return theme;
  }

  static const CliTheme& Dark()
  {
    static const CliTheme theme = {
      .section_header = fmt::fg(fmt::color::cyan) | fmt::emphasis::bold,
      .command_name = fmt::fg(fmt::color::yellow) | fmt::emphasis::bold,
      .option_flag = fmt::fg(fmt::color::green) | fmt::emphasis::bold,
      .placeholder = fmt::fg(fmt::color::magenta) | fmt::emphasis::italic,
      .note = fmt::fg(fmt::color::red) | fmt::emphasis::bold,
      .default_value = fmt::fg(fmt::color::blue),
      .example = fmt::fg(fmt::color::white) | fmt::emphasis::italic,
      .reset = fmt::text_style(),
    };
    return theme;
  }

  static const CliTheme& Light()
  {
    static const CliTheme theme = {
      .section_header = fmt::fg(fmt::color::blue) | fmt::emphasis::bold,
      .command_name = fmt::fg(fmt::color::yellow) | fmt::emphasis::bold,
      .option_flag = fmt::fg(fmt::color::green) | fmt::emphasis::bold,
      .placeholder = fmt::fg(fmt::color::magenta) | fmt::emphasis::italic,
      .note = fmt::fg(fmt::color::red) | fmt::emphasis::bold,
      .default_value = fmt::fg(fmt::color::navy),
      .example = fmt::fg(fmt::color::black) | fmt::emphasis::italic,
      .reset = fmt::text_style(),
    };
    return theme;
  }
};

} // namespace oxygen::clap
