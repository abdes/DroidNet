//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Composition/Composition.h>
#include <Oxygen/Composition/Named.h>
#include <Oxygen/Composition/ObjectMetaData.h>

namespace {

using oxygen::Composition;
using oxygen::Named;
using oxygen::ObjectMetaData;

class Example final : public Composition, public Named {
    OXYGEN_TYPED(Example)
public:
    Example()
    {
        AddComponent<ObjectMetaData>("Example");
    }

    auto GetName() const noexcept -> std::string_view override
    {
        return GetComponent<ObjectMetaData>().GetName();
    }

    void SetName(const std::string_view name) noexcept override
    {
        GetComponent<ObjectMetaData>().SetName(name);
    }
};

} // namespace

auto main(int argc, char** argv) -> int
{
    loguru::g_preamble_date = false;
    loguru::g_preamble_time = false;
    loguru::g_preamble_uptime = false;
    loguru::g_preamble_thread = false;
    loguru::g_preamble_header = false;
    loguru::g_stderr_verbosity = loguru::Verbosity_INFO;
    loguru::init(argc, argv);

    auto status = EXIT_SUCCESS;
    try {
        const Example example;
        LOG_F(INFO, "Hello from: {}", example.GetName());
    } catch (const std::exception& e) {
        LOG_F(ERROR, "Exception caught: {}", e.what());
        status = EXIT_FAILURE;
    }

    loguru::shutdown();
    return status;
}
