// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Storage;

namespace Oxygen.Editor.Documents;

/// <summary>What a relocation did to an open document's file.</summary>
/// <param name="File">The document's file after the relocation; the same file when only its references moved.</param>
/// <param name="Version">The file's version after the relocation, whether it rewrote the file or not.</param>
/// <param name="MovedAssets">The asset URIs the relocation moved, from their old URI to their new one.</param>
public sealed record DocumentRelocation(DocumentFile File, FileVersion Version, IReadOnlyDictionary<Uri, Uri> MovedAssets);
