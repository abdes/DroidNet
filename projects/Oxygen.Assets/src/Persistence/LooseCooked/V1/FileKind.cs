// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Assets.Persistence.LooseCooked.V1;

/// <summary>
/// Kind of file record. These map to <c>oxygen::data::loose_cooked::v1::FileKind</c>.
/// </summary>
public enum FileKind : ushort
{
    Unknown = 0,
    BuffersTable = 1,
    BuffersData = 2,
    TexturesTable = 3,
    TexturesData = 4,
}
