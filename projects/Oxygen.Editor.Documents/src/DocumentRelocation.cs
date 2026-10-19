// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Storage;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Documents;

/// <summary>What a relocation did to an open document's file.</summary>
/// <param name="File">The document's file after the relocation; the same file when only its references moved.</param>
/// <param name="Version">The file's version after the relocation, whether it rewrote the file or not.</param>
/// <param name="MapAsset">
/// Maps a referenced asset to where it moved; it returns <see langword="null"/> when the asset did not move. A folder
/// that moved moves every asset inside it.
/// </param>
public sealed record DocumentRelocation(DocumentFile File, FileVersion Version, Func<AssetPath, AssetPath?> MapAsset);
