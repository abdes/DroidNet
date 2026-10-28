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
   * \brief The arguments of a command line, as `main` receives them.
   *
   * \param [in] args The program name, then each argument, as null-terminated
   * strings; not empty.
   */
  OXGN_CLP_API explicit Arguments(std::span<const char* const> args);

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
