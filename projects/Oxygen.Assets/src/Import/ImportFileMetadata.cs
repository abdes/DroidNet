// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Assets.Import;

/// <summary>
/// Basic file metadata used for incremental import decisions.
/// </summary>
/// <param name="ByteLength">File size in bytes.</param>
/// <param name="LastWriteTimeUtc">File last write time (UTC).</param>
public sealed record ImportFileMetadata(
    long ByteLength,
    DateTimeOffset LastWriteTimeUtc);
