// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Assets.Persistence.LooseCooked.V1;

/// <summary>
/// Known v1 index flags. These map to <c>oxygen::data::loose_cooked::v1::IndexFeatures</c>.
/// </summary>
[Flags]
public enum IndexFeatures : uint
{
    None = 0,
    HasVirtualPaths = 1u << 0,
    HasFileRecords = 1u << 1,
}
