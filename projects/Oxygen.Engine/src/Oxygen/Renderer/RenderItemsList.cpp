//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <stdexcept>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Renderer/RenderItemsList.h>

namespace oxygen::engine {

namespace {
  inline auto IsValidAabb(const glm::vec3& mn, const glm::vec3& mx) -> bool
  {
    return mn.x <= mx.x && mn.y <= mx.y && mn.z <= mx.z;
  }
}

auto RenderItemsList::Validate(const RenderItem& item) -> void
{
  // Sphere radius must be non-negative when provided.
  if (item.bounding_sphere.w < 0.0f) {
    LOG_F(
      ERROR, "RenderItem validation failed: negative bounding sphere radius");
    throw std::invalid_argument("negative bounding sphere radius");
  }
  // AABB must satisfy min <= max on each component.
  if (!IsValidAabb(item.bounding_box_min, item.bounding_box_max)) {
    LOG_F(ERROR, "RenderItem validation failed: invalid AABB min/max ordering");
    throw std::invalid_argument("invalid AABB min/max ordering");
  }
}

auto RenderItemsList::Recompute(RenderItem& item) -> void
{
  // Always recompute transformed properties to avoid stale state.
  item.UpdateComputedProperties();
}

auto RenderItemsList::Clear() -> void { items_.clear(); }

auto RenderItemsList::Reserve(const std::size_t n) -> void
{
  items_.reserve(n);
}

auto RenderItemsList::Add(RenderItem item) -> std::size_t
{
  // Validate current inputs then recompute derived state.
  Validate(item);
  Recompute(item);
  items_.push_back(std::move(item));
  return items_.size() - 1;
}

auto RenderItemsList::RemoveAt(const std::size_t index) -> void
{
  if (index >= items_.size()) {
    throw std::out_of_range("RenderItemsList::RemoveAt index out of range");
  }
  items_.erase(items_.begin() + static_cast<std::ptrdiff_t>(index));
}

auto RenderItemsList::Items() const -> std::span<const RenderItem>
{
  return { items_.data(), items_.size() };
}

auto RenderItemsList::Update(const std::size_t index, const RenderItem& item)
  -> void
{
  if (index >= items_.size()) {
    throw std::out_of_range("RenderItemsList::Update index out of range");
  }
  auto copy = item;
  Validate(copy);
  Recompute(copy);
  items_[index] = std::move(copy);
}

auto RenderItemsList::Size() const -> std::size_t { return items_.size(); }

} // namespace oxygen::engine
