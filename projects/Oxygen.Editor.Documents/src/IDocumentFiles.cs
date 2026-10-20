// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Managed.Core;

namespace Oxygen.Editor.Documents;

/// <summary>
/// Where the project keeps the files that documents edit. The module that owns the project's folders implements it.
/// </summary>
public interface IDocumentFiles
{
    /// <summary>Finds the file an authored asset is edited in.</summary>
    /// <param name="asset">The asset.</param>
    /// <returns>The asset's file, or <see langword="null"/> when the asset is not authored in the project's content folders.</returns>
    public DocumentFile? Find(AssetPath asset);

    /// <summary>Finds the document file at a path inside the project's content folders.</summary>
    /// <param name="path">The file's path; the file need not exist.</param>
    /// <returns>The file, or <see langword="null"/> when the path is outside the project's content folders.</returns>
    public DocumentFile? Find(FileSystemPath path);
}
