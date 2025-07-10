//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause).
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <span>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Clap/Internal/Args.h>

class asap::clap::detail::Arguments::ArgumentsImpl {
public:
  ArgumentsImpl(int argc, const char** argv)
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

asap::clap::detail::Arguments::Arguments(int argc, const char** argv)
  : impl_(new ArgumentsImpl(argc, argv),
      // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
      [](const ArgumentsImpl* impl) { delete impl; })
{
}

auto asap::clap::detail::Arguments::ProgramName() const -> const std::string&
{
  return impl_->program_name;
}
auto asap::clap::detail::Arguments::Args() const -> std::vector<std::string>&
{
  return impl_->args;
}
