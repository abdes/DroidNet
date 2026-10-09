// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.ContentPipeline.Relocation;

namespace Oxygen.Editor.ContentBrowser.Messages;

/// <summary>
/// Sent after the Content Browser moved, rewrote or deleted authored files. The sources tree follows moved folders
/// and documents of deleted files close; open documents follow renames and moves in memory, as relocation
/// participants, before this message.
/// </summary>
/// <param name="Moves">The physical moves, including companion files and folders.</param>
/// <param name="RewrittenFiles">The absolute locations of the rewritten files, after the moves.</param>
/// <param name="DeletedFiles">The absolute files and folders moved to the Recycle Bin.</param>
public sealed record AssetFilesChangedMessage(
    IReadOnlyList<RelocationFileMove> Moves,
    IReadOnlyList<string> RewrittenFiles,
    IReadOnlyList<string> DeletedFiles)
{
    /// <summary>Gets a value indicating whether a folder moved or was deleted, so the sources tree must reload.</summary>
    public bool FoldersChanged { get; init; }
}
