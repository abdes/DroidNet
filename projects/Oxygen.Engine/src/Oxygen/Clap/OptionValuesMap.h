//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include <Oxygen/Clap/OptionValue.h>

namespace oxygen::clap {

//! Stores and retrieves all values for each parsed command-line option.
/*!
  This class is used by the command-line parser to accumulate all values
  associated with each option as they are parsed from the command line.

  ### Key Features
  - Supports options that may occur multiple times (e.g., repeated flags).
  - Provides efficient lookup for all values of a given option.
  - Allows checking for presence and counting occurrences of options.

  ### Usage Example
  ```cpp
  OptionValuesMap ovm;
  ovm.StoreValue("--file", OptionValue("foo.txt"));
  ovm.StoreValue("--file", OptionValue("bar.txt"));
  auto files = ovm.ValuesOf("--file"); // contains both values
  ```

  @see OptionValue
*/
class OptionValuesMap {
public:
  //! Construct an empty option values map.
  OptionValuesMap() = default;

  OptionValuesMap(const OptionValuesMap&) = delete;
  OptionValuesMap(OptionValuesMap&&) noexcept = default;

  auto operator=(const OptionValuesMap&) -> OptionValuesMap& = delete;
  auto operator=(OptionValuesMap&&) -> OptionValuesMap& = delete;

  ~OptionValuesMap() = default;

  //! Store a value for the given option name.
  /*!
    Adds a new value for the specified option. If the option already exists,
    the value is appended to its vector; otherwise, a new entry is created.

    @param option_name The name of the option (e.g., "--file").
    @param new_value The value to store for this option.
    @see ValuesOf, OccurrencesOf
  */
  auto StoreValue(const std::string& option_name, OptionValue new_value) -> void
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

  //! Get all values for a given option name.
  /*!
    Returns a const reference to the vector of all values stored for the
    specified option. Throws std::out_of_range if the option is not present.

    @param option_name The name of the option.
    @return A const reference to the vector of values for this option.
    @see StoreValue, HasOption
  */
  [[nodiscard]] auto ValuesOf(const std::string& option_name) const
    -> const std::vector<OptionValue>&
  {
    static const std::vector<OptionValue> empty;
    if (const auto it = ovm_.find(option_name); it != ovm_.end()) {
      return it->second;
    }
    return empty;
  }

  //! Check if an option was provided on the command line.
  /*!
    Returns true if the specified option was present (i.e., has at least one
    value).

    @param option_name The name of the option.
    @return True if the option was provided, false otherwise.
    @see StoreValue, ValuesOf
  */
  [[nodiscard]] auto HasOption(const std::string& option_name) const -> bool
  {
    return ovm_.contains(option_name);
  }

  //! Get the number of times an option was provided.
  /*!
    Returns the number of values stored for the specified option (i.e., the
    number of occurrences).

    @param option_name The name of the option.
    @return The number of times the option was provided.
    @see StoreValue
  */
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

} // namespace oxygen::clap
