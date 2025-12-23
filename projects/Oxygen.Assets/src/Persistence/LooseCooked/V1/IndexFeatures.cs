// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;

namespace Oxygen.Assets.Persistence.LooseCooked.V1;

/// <summary>
/// Known v1 index flags. These map to <c>oxygen::data::loose_cooked::v1::IndexFeatures</c>.
/// </summary>
[Flags]
[SuppressMessage(
    "Design",
    "CA1028:Enum Storage should be Int32",
    Justification = "This enum is serialized to/from the v1 index binary format and must match the on-disk uint layout.")]
public enum IndexFeatures : uint
{
    None = 0,
    HasVirtualPaths = 1u << 0,
    HasFileRecords = 1u << 1,
}
