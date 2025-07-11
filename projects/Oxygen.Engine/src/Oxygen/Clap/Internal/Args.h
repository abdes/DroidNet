//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <span>
#include <string>
#include <string_view>

#include <Oxygen/Clap/api_export.h>

namespace oxygen::clap::detail {

/**
 * \brief A safer type to encapsulate the program's `argc` and `argv`.
 *
 * This class has been designed to be safe because the program arguments
 * are stored using a safe C++ container, separate from the program name and
 * accessible only via safe accessor methods. In addition to being a safer
 * implementation, this will eliminate many linter warnings and static analysis
 * complaints.
 */
class Arguments {
public:
  /*!
   * \brief Constructor to automatically covert from {argc, argv} to safer
   * types.
   *
   * \param [in] argc Non-negative value representing the number of arguments
   * passed to the program from the environment in which the program is run.
   *
   * \param [in] argv Pointer to the first element of an array of `argc + 1`
   * pointers, of which the last one is null and the previous ones, if any,
   * point to null-terminated multi-byte strings that represent the arguments
   * passed to the program from the execution environment. If `argv[0]` is not a
   * null pointer (or, equivalently, if `argc > 0`), it points to a string that
   * represents the name used to invoke the program, or to an empty string.
   */
  OXGN_CLP_API Arguments(int argc, const char** argv);

  /*!
   * \brief The program name, originally provided as the first element of the
   * `argv` array.
   */
  OXGN_CLP_NDAPI auto ProgramName() const -> std::string_view;

  /*!
   * \brief The program command line arguments, excluding the program name.
   *
   * \see ProgramName
   */
  OXGN_CLP_NDAPI auto Args() const -> std::span<const std::string>;

private:
  struct ArgumentsImpl;

  // Stores the implementation and the implementation's deleter as well to work
  // around the fact that the implementation class is an incomplete type so far.
  // Deleter is a pointer to a function with signature
  // `void func(ArgumentsImpl *)`.
  // https://oliora.github.io/2015/12/29/pimpl-and-rule-of-zero.html
  std::unique_ptr<ArgumentsImpl, void (*)(const ArgumentsImpl*)> impl_;
};

} // namespace oxygen::clap::detail
