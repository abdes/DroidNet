// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Managed.Core;

namespace Oxygen.Editor.Documents;

/// <summary>Names the files a change in the editor deleted.</summary>
/// <param name="files">The deleted files and folders.</param>
public sealed class DocumentFilesDeletedEventArgs(IReadOnlyList<FileSystemPath> files) : EventArgs
{
    /// <summary>Gets the deleted files and folders; a deleted folder deletes every file inside it.</summary>
    public IReadOnlyList<FileSystemPath> Files { get; } = files;
}
