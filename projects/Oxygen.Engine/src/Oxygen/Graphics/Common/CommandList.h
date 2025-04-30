//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <string_view>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Composition/Composition.h>
#include <Oxygen/Composition/Named.h>
#include <Oxygen/Composition/ObjectMetaData.h>
#include <Oxygen/Graphics/Common/Types/Queues.h>
#include <Oxygen/Graphics/Common/api_export.h>

namespace oxygen::graphics {

class CommandList : public Composition, public Named {
public:
    explicit CommandList(QueueRole type)
        : CommandList(type, "Command List")
    {
    }

    CommandList(QueueRole type, std::string_view name)
        : type_(type)
    {
        AddComponent<ObjectMetaData>(name);
    }

    ~CommandList() override = default;

    OXYGEN_MAKE_NON_COPYABLE(CommandList);
    OXYGEN_MAKE_NON_MOVABLE(CommandList);

    [[nodiscard]] auto GetQueueType() const { return type_; }

    [[nodiscard]] auto GetName() const noexcept -> std::string_view override
    {
        return GetComponent<ObjectMetaData>().GetName();
    }

    void SetName(std::string_view name) noexcept override
    {
        GetComponent<ObjectMetaData>().SetName(name);
    }

private:
    QueueRole type_ { QueueRole::kNone };
};

} // namespace oxygen::graphics
