//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <string>

#include "Oxygen/Base/Logging.h"
#include "Oxygen/Base/Windows/StringUtils.h"

int main(int argc, char** argv)
{
    loguru::g_preamble_date = false;
    loguru::g_preamble_time = false;
    loguru::g_preamble_uptime = false;
    loguru::g_preamble_thread = false;
    loguru::g_preamble_header = false;
    loguru::g_stderr_verbosity = loguru::Verbosity_INFO;
    loguru::init(argc, argv);

    std::string utf8_str {};
    oxygen::string_utils::WideToUtf8(L"Hello World!", utf8_str);
    LOG_F(INFO, "{}", utf8_str.c_str());

    loguru::shutdown();
}
