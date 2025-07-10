//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause).
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include <Oxygen/Clap/OptionValue.h>

namespace asap::clap {

class OptionValuesMap {
public:
  OptionValuesMap() = default;

  OptionValuesMap(const OptionValuesMap&) = delete;
  OptionValuesMap(OptionValuesMap&&) = default;

  auto operator=(const OptionValuesMap&) -> OptionValuesMap& = delete;
  auto operator=(OptionValuesMap&&) -> OptionValuesMap& = delete;

  ~OptionValuesMap() = default;

  void StoreValue(const std::string& option_name, OptionValue new_value)
  {
    const auto in_ovm = ovm_.find(option_name);
    if (in_ovm != ovm_.end()) {
      auto& option_values = in_ovm->second;
      option_values.emplace_back(std::move(new_value));
      return;
    }
    ovm_.emplace(
      option_name, std::vector<OptionValue> { std::move(new_value) });
  }

  [[nodiscard]] auto ValuesOf(const std::string& option_name) const
    -> const std::vector<OptionValue>&
  {
    return ovm_.at(option_name);
  }

  [[nodiscard]] auto HasOption(const std::string& option_name) const -> bool
  {
    return ovm_.find(option_name) != ovm_.cend();
  }

  [[nodiscard]] auto OccurrencesOf(const std::string& option_name) const
    -> size_t
  {
    if (const auto option = ovm_.find(option_name); option != ovm_.cend()) {
      return option->second.size();
    }
    return 0;
  }

private:
  std::unordered_map<std::string, std::vector<OptionValue>> ovm_;
};

} // namespace asap::clap
