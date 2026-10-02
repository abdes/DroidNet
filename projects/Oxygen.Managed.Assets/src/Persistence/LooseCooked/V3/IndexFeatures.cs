// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;

namespace Oxygen.Managed.Assets.Persistence.LooseCooked.V3;

/// <summary>Known feature flags in the current loose cooked index.</summary>
[Flags]
[SuppressMessage(
    "Design",
    "CA1028:Enum Storage should be Int32",
    Justification = "This enum is serialized as an on-disk uint.")]
public enum IndexFeatures : uint
{
    None = 0,
    HasVirtualPaths = 1u << 0,
    HasFileRecords = 1u << 1,
}
