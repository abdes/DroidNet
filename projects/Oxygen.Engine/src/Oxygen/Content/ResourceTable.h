//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <optional>

#include <Oxygen/Content/ResourceTypeList.h>
#include <Oxygen/Data/PakFormat.h>

namespace oxygen::content {

//! ResourceTable: Lightweight offset resolver for PAK resource descriptors
/*!
 ResourceTable provides a type-safe, lightweight mechanism for resolving
 resource descriptor offsets within a PAK file. It is parameterized by the
 resource type and uses metadata from the PAK file to compute descriptor
 locations and validate resource keys.

 ### Key Features

 - **Type-Safe Offset Resolution**: Uses resource type metadata to ensure
   correct offset calculations.
 - **Validation**: Checks resource key bounds and entry size consistency.
 - **No Resource Ownership**: Does not own or manage resource data, only
   descriptor offsets.

 ### Usage Patterns

 ```cpp
 // Example usage
 using TextureTable = ResourceTable<TextureResource>;
 TextureTable table(meta);
 auto offset = table.GetResourceOffset(key);
 ```

 ### Architecture Notes

 - Designed for bindless resource management in Oxygen Engine.
 - Used by resource loaders and registry systems for fast descriptor lookup.

 @tparam T Resource type (must satisfy PakResource concept)
 @see oxygen::content::PakResource, data::pak::ResourceTable
*/
template <PakResource T> class ResourceTable {
public:
  using DescT = typename T::DescT;
  using ResourceKeyT = data::pak::ResourceIndexT;

  //! Construct a ResourceTable with resource table metadata
  /*!
   Initializes a ResourceTable as a lightweight offset resolver using the
   provided resource table metadata (from the PAK file). The metadata describes
   the absolute offset, entry count, and entry size of the resource table within
   the PAK file.

   @param table_meta  ResourceTable metadata struct (from the PAK file).

   @note The entry_size field in table_meta must match sizeof(DescT).
   @throw std::invalid_argument if entry_size does not match the expected size.
  */
  explicit ResourceTable(const data::pak::ResourceTable& table_meta)
    : table_meta_(table_meta)
  {
    // Validate entry size or abort
    constexpr std::size_t expected_entry_size = sizeof(DescT);
    CHECK_EQ_F(table_meta.entry_size, expected_entry_size,
      "ResourceTable: entry_size does not match expected size");
  }

  //! Get the file offset for a resource key
  /*!
   Returns the absolute file offset where the resource descriptor for the given
   key is located in the PAK file.

   @param key The resource key to resolve
   @return File offset if key is valid, nullopt otherwise

   @note The returned offset points to the resource descriptor, not the resource
   data itself.
  */
  [[nodiscard]] auto GetResourceOffset(const ResourceKeyT& key) const noexcept
    -> std::optional<data::pak::OffsetT>
  {
    if (!IsValidKey(key)) {
      return std::nullopt;
    }
    return table_meta_.offset
      + static_cast<std::uint64_t>(key) * table_meta_.entry_size;
  }

  //! Check if a resource key is valid (within table bounds)
  [[nodiscard]] auto IsValidKey(const ResourceKeyT& key) const noexcept -> bool
  {
    return key < table_meta_.count;
  }

  //! Returns the number of resources described in the table
  [[nodiscard]] auto Size() const noexcept -> data::pak::ResourceIndexT
  {
    return table_meta_.count;
  }

private:
  data::pak::ResourceTable table_meta_;
};

} // namespace oxygen::content
