// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Assets.Persistence.LooseCooked.V1;

public sealed record FileRecord(
    FileKind Kind,
    string RelativePath,
    ulong Size,
    System.ReadOnlyMemory<byte> Sha256);
