// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Models;

/// <summary>A file an import of a model read besides its primary source.</summary>
/// <param name="Path">The file, relative to the primary source's folder.</param>
/// <param name="Size">The file's size, in bytes.</param>
/// <param name="Sha256">The file's SHA-256, lowercase hexadecimal.</param>
public sealed record ModelSourceFile(string Path, long Size, string Sha256);
