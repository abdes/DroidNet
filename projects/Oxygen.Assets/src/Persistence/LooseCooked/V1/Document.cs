// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Assets.Persistence.LooseCooked.V1;

public sealed record Document(
    ushort ContentVersion,
    IndexFeatures Flags,
    Guid SourceGuid,
    IReadOnlyList<AssetEntry> Assets,
    IReadOnlyList<FileRecord> Files);
