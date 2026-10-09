// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Storage;

namespace Oxygen.Editor.ContentPipeline.Relocation;

/// <summary>One file a relocation rewrote.</summary>
/// <param name="OriginalPath">The file's location before the moves.</param>
/// <param name="Path">The file's location after the moves.</param>
/// <param name="Written">The version the transaction wrote, which an open document records as saved.</param>
public sealed record RelocatedFile(string OriginalPath, string Path, FileVersion Written);
