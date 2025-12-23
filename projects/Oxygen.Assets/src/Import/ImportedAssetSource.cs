// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Assets.Import;

/// <summary>
/// Source fingerprint information used for incremental decisions.
/// </summary>
/// <param name="SourcePath">Project-relative source path.</param>
/// <param name="SourceHashSha256">SHA-256 hash of the source content.</param>
/// <param name="LastWriteTimeUtc">Source last write time.</param>
public sealed record ImportedAssetSource(
    string SourcePath,
    ReadOnlyMemory<byte> SourceHashSha256,
    DateTimeOffset LastWriteTimeUtc);
