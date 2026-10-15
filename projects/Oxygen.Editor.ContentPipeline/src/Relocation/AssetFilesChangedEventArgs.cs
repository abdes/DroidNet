// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ContentPipeline.Relocation;

/// <summary>The files a committed relocation or delete changed, with the exact moves the editor made.</summary>
/// <param name="moves">The physical moves, including companion files and folders.</param>
/// <param name="rewrittenFiles">The absolute locations of the files whose references were rewritten, after the moves.</param>
/// <param name="deletedFiles">The absolute files and folders moved to the Recycle Bin.</param>
/// <param name="foldersChanged">Whether a folder moved or was deleted.</param>
public sealed class AssetFilesChangedEventArgs(
    IReadOnlyList<RelocationFileMove> moves,
    IReadOnlyList<string> rewrittenFiles,
    IReadOnlyList<string> deletedFiles,
    bool foldersChanged) : EventArgs
{
    /// <summary>Gets the physical moves, including companion files and folders.</summary>
    public IReadOnlyList<RelocationFileMove> Moves { get; } = moves;

    /// <summary>Gets the absolute locations of the files whose references were rewritten, after the moves.</summary>
    public IReadOnlyList<string> RewrittenFiles { get; } = rewrittenFiles;

    /// <summary>Gets the absolute files and folders moved to the Recycle Bin.</summary>
    public IReadOnlyList<string> DeletedFiles { get; } = deletedFiles;

    /// <summary>Gets a value indicating whether a folder moved or was deleted.</summary>
    public bool FoldersChanged { get; } = foldersChanged;
}
