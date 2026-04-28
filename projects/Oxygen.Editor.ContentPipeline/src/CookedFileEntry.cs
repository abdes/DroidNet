// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ContentPipeline;

/// <summary>
/// Cooked file entry returned by cooked-output inspection.
/// </summary>
/// <param name="RelativePath">The file path relative to the cooked root.</param>
/// <param name="Size">The file size in bytes.</param>
public sealed record CookedFileEntry(string RelativePath, ulong Size);
