// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;

namespace Oxygen.Managed.Assets.Persistence.LooseCooked.V3;

/// <summary>File-record kinds in the current loose cooked index.</summary>
[SuppressMessage(
    "Design",
    "CA1028:Enum Storage should be Int32",
    Justification = "This enum is serialized as an on-disk ushort.")]
public enum FileKind : ushort
{
    Unknown = 0,
    BuffersTable = 1,
    BuffersData = 2,
    TexturesTable = 3,
    TexturesData = 4,
    ScriptsTable = 5,
    ScriptsData = 6,
    PhysicsTable = 7,
    PhysicsData = 8,
    Auxiliary = 11,
}
