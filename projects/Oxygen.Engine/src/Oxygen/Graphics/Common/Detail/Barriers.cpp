//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <sstream>
#include <string>

#include <Oxygen/Base/NoStd.h>
#include <Oxygen/Graphics/Common/Detail/Barriers.h>

using oxygen::graphics::detail::Barrier;

auto oxygen::graphics::detail::to_string(const Barrier& barrier) -> std::string
{
    return std::visit(
        []<typename TDescriptor>(const TDescriptor& desc) -> std::string {
            using T = std::decay_t<TDescriptor>;
            std::ostringstream oss;
            if constexpr (std::is_same_v<T, MemoryBarrierDesc>) {
                oss << "Memory Barrier for resource " << desc.resource.AsInteger();
            } else if constexpr (std::is_same_v<T, BufferBarrierDesc>) {
                oss << "Buffer Barrier for resource " << desc.resource.AsInteger()
                    << ": " << nostd::to_string(desc.before)
                    << " -> " << nostd::to_string(desc.after);
            } else if constexpr (std::is_same_v<T, TextureBarrierDesc>) {
                oss << "Texture Barrier for resource " << desc.resource.AsInteger()
                    << ": " << nostd::to_string(desc.before)
                    << " -> " << nostd::to_string(desc.after);
            }
            return oss.str();
        },
        barrier.GetDescriptor());
}
