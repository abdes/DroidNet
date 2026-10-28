//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <span>
#include <string_view>
#include <vector>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Clap/Internal/Args.h>

struct oxygen::clap::detail::Arguments::ArgumentsImpl {
  explicit ArgumentsImpl(const std::span<const char* const> command_line)
  {
    DCHECK_F(!command_line.empty());

    // The program name comes first; the rest are the arguments to parse.
    program_name.assign(command_line.front());
    const auto arguments = command_line.subspan(1);
    args.assign(arguments.begin(), arguments.end());

    DCHECK_F(!program_name.empty());
  }

  std::string program_name;
  std::vector<std::string> args;
};

oxygen::clap::detail::Arguments::Arguments(
  const std::span<const char* const> args)
  : impl_(new ArgumentsImpl(args),
      // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
      [](const ArgumentsImpl* impl) -> void { delete impl; })
{
}

auto oxygen::clap::detail::Arguments::ProgramName() const -> std::string_view
{
  return impl_->program_name;
}

auto oxygen::clap::detail::Arguments::Args() const
  -> std::span<const std::string>
{
  return { impl_->args };
}
