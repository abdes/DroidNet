//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <vector>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Clap/Internal/Args.h>

struct oxygen::clap::detail::Arguments::ArgumentsImpl {
  ArgumentsImpl(const int argc, const char** argv)
  {
    DCHECK_GT_F(argc, 0);
    DCHECK_NOTNULL_F(argv);

    auto s_argv = std::span(argv, static_cast<size_t>(argc));
    // Extract the program name from the first argument (should always be there)
    // and keep the rest of the arguments for later parsing
    program_name.assign(s_argv[0]);
    if (argc > 1) {
      args.assign(++s_argv.begin(), s_argv.end());
    }

    DCHECK_F(!program_name.empty());
  }

  std::string program_name;
  std::vector<std::string> args {};
};

oxygen::clap::detail::Arguments::Arguments(int argc, const char** argv)
  : impl_(new ArgumentsImpl(argc, argv),
      // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
      [](const ArgumentsImpl* impl) { delete impl; })
{
}

auto oxygen::clap::detail::Arguments::ProgramName() const -> std::string_view
{
  return impl_->program_name;
}

auto oxygen::clap::detail::Arguments::Args() const
  -> std::span<const std::string>
{
  return std::span<const std::string>(impl_->args);
}
