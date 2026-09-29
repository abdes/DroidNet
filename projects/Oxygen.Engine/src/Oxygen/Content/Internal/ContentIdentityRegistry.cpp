//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory_resource>
#include <optional>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <variant>

#include <Oxygen/Content/Internal/ContentIdentity.h>
#include <Oxygen/Content/Internal/ContentIdentityRegistry.h>
#include <Oxygen/Data/SourceOrigin.h>

namespace oxygen::content::internal {

ContentIdentityRegistry::ContentIdentityRegistry(
  std::pmr::memory_resource& memory, const HashFunction hash)
  : identities_(0, hash, &memory)
  , identities_by_id_(&memory)
{
  if (hash == nullptr) {
    throw std::invalid_argument("Content identity hashing must be callable");
  }
}

auto ContentIdentityRegistry::HashIdentity(
  const ContentIdentity& identity) noexcept -> size_t
{
  return ContentIdentityHash {}(identity);
}

auto ContentIdentityRegistry::Intern(const ContentIdentity& identity)
  -> ContentId
{
  if (const auto found = identities_.find(identity);
    found != identities_.end()) {
    return found->second;
  }
  if (next_id_ == std::numeric_limits<uint64_t>::max()) {
    throw std::length_error("Content identity namespace is exhausted");
  }

  const ContentId id { next_id_ };
  const auto [entry, inserted] = identities_.emplace(identity, id);
  if (!inserted) {
    return entry->second;
  }
  try {
    identities_by_id_.emplace(id, &entry->first);
  } catch (...) {
    identities_.erase(entry);
    throw;
  }
  ++next_id_;
  return id;
}

auto ContentIdentityRegistry::Find(
  const ContentIdentity& identity) const noexcept -> ContentId
{
  const auto found = identities_.find(identity);
  return found == identities_.end() ? ContentId {} : found->second;
}

auto ContentIdentityRegistry::Resolve(const ContentId id) const noexcept
  -> const ContentIdentity*
{
  const auto found = identities_by_id_.find(id);
  return found == identities_by_id_.end() ? nullptr : found->second;
}

auto ContentIdentityRegistry::Erase(const ContentId id) -> void
{
  const auto found = identities_by_id_.find(id);
  if (found == identities_by_id_.end()) {
    return;
  }
  identities_.erase(*found->second);
  identities_by_id_.erase(found);
}

auto ContentIdentityRegistry::FindAsset(const ContentId id) const noexcept
  -> const AssetIdentity*
{
  const auto* identity = Resolve(id);
  return identity == nullptr ? nullptr : std::get_if<AssetIdentity>(identity);
}

auto ContentIdentityRegistry::FindCookedResource(
  const ContentId id) const noexcept -> const CookedResourceIdentity*
{
  const auto* identity = Resolve(id);
  return identity == nullptr ? nullptr
                             : std::get_if<CookedResourceIdentity>(identity);
}

auto ContentIdentityRegistry::FindResourceKind(
  const ContentId id) const noexcept -> std::optional<ResourceKind>
{
  const auto* identity = Resolve(id);
  if (identity == nullptr) {
    return std::nullopt;
  }
  if (const auto* cooked = std::get_if<CookedResourceIdentity>(identity)) {
    return cooked->kind;
  }
  if (const auto* synthetic
    = std::get_if<SyntheticResourceIdentity>(identity)) {
    return synthetic->kind;
  }
  return std::nullopt;
}

auto ContentIdentityRegistry::Clear() noexcept -> void
{
  identities_by_id_.clear();
  identities_.clear();
}

auto ContentIdentityRegistry::EraseSources(
  const std::unordered_set<data::SourceInstanceId>& sources) -> void
{
  if (sources.empty()) {
    return;
  }
  for (auto entry = identities_.begin(); entry != identities_.end();) {
    const auto source = std::visit(
      [](const auto& identity) {
        if constexpr (requires { identity.source; }) {
          return identity.source;
        } else {
          return data::SourceInstanceId {};
        }
      },
      entry->first);
    if (sources.contains(source)) {
      identities_by_id_.erase(entry->second);
      entry = identities_.erase(entry);
    } else {
      ++entry;
    }
  }
}

auto ContentIdentityRegistry::Size() const noexcept -> size_t
{
  return identities_.size();
}

auto ContentIdentityRegistry::Entries() const noexcept
  -> const std::pmr::unordered_map<ContentId, const ContentIdentity*>&
{
  return identities_by_id_;
}

} // namespace oxygen::content::internal
