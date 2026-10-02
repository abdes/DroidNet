// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Managed.Assets.Persistence.LooseCooked.V3;

/// <summary>Opaque location and counts for one asset's native reference block.</summary>
/// <remarks>The native Data and Content modules own block validation and interpretation.</remarks>
public readonly record struct AssetReferenceTable(ulong Offset, uint ResourceCount, uint KeyCount);
