// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ContentPipeline.Import;

/// <summary>Physical bytes owned by a capture operation.</summary>
/// <param name="Path">Absolute private file path.</param>
/// <param name="Size">Captured length in bytes.</param>
/// <param name="Sha256">Lowercase SHA-256 of the complete captured file.</param>
public sealed record NativeCapturedFile(string Path, ulong Size, string Sha256);
