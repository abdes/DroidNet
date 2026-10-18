// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Managed.Core;

namespace Oxygen.Editor.ContentPipeline.Relocation;

/// <summary>The files the editor changed, with the exact moves, copies and deletes it made.</summary>
/// <param name="moves">The physical moves, including companion files and folders.</param>
/// <param name="rewrittenFiles">The absolute locations of the files whose references were rewritten, after the moves.</param>
/// <param name="deletedFiles">The absolute files and folders moved to the Recycle Bin.</param>
/// <param name="foldersChanged">Whether a folder moved, was created or was deleted.</param>
public sealed class AssetFilesChangedEventArgs(
    IReadOnlyList<RelocationFileMove> moves,
    IReadOnlyList<FileSystemPath> rewrittenFiles,
    IReadOnlyList<FileSystemPath> deletedFiles,
    bool foldersChanged) : EventArgs
{
    /// <summary>Gets the physical moves, including companion files and folders.</summary>
    public IReadOnlyList<RelocationFileMove> Moves { get; } = moves;

    /// <summary>Gets the absolute locations of the files whose references were rewritten, after the moves.</summary>
    public IReadOnlyList<FileSystemPath> RewrittenFiles { get; } = rewrittenFiles;

    /// <summary>Gets the absolute files and folders moved to the Recycle Bin.</summary>
    public IReadOnlyList<FileSystemPath> DeletedFiles { get; } = deletedFiles;

    /// <summary>Gets a value indicating whether a folder moved, was created or was deleted.</summary>
    public bool FoldersChanged { get; } = foldersChanged;

    /// <summary>Gets the absolute files and folders created, such as copies and new scripts.</summary>
    public IReadOnlyList<FileSystemPath> CreatedFiles { get; private init; } = [];

    /// <summary>Reports files and folders the editor created and changed nothing else.</summary>
    /// <param name="createdFiles">The absolute files and folders created.</param>
    /// <param name="foldersCreated">Whether a folder was created.</param>
    /// <returns>The report.</returns>
    public static AssetFilesChangedEventArgs ForCreated(IReadOnlyList<FileSystemPath> createdFiles, bool foldersCreated)
        => new([], [], [], foldersCreated) { CreatedFiles = createdFiles };
}
