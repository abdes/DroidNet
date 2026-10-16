// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.TimeMachine;

namespace Oxygen.Editor.ContentPipeline.Relocation;

/// <summary>
/// Renames, moves, copies and deletes authored assets while keeping every authored reference to them
/// valid. Changes run as the content cook coordinator's project writer, so they never overlap a cook.
/// </summary>
/// <remarks>
/// Nothing here cooks: the changed files reach the cook through the asset catalog like any other change, and cook on
/// change or the user cooks them.
/// </remarks>
public interface IAssetRelocationService
{
    /// <summary>
    /// Occurs after a relocation or a delete is committed, with the exact files it moved, rewrote or deleted. It is
    /// raised on the synchronization context of the operation's caller, after open documents followed the change.
    /// </summary>
    /// <remarks>
    /// Only changes made through this service are reported. Files changed outside the editor reach the asset
    /// catalogs through file watching, which sees a move as a removal and an addition.
    /// </remarks>
    public event EventHandler<AssetFilesChangedEventArgs>? FilesChanged;

    /// <summary>Registers the owner of open documents, which follows every committed relocation in memory.</summary>
    /// <param name="participant">The owner.</param>
    /// <returns>A registration disposed when the document closes.</returns>
    public IDisposable AddParticipant(IAssetRelocationParticipant participant);

    /// <summary>
    /// Relocates assets and rewrites their referrers in one restorable transaction, brings open documents in step,
    /// and records the relocation in <paramref name="history"/>.
    /// </summary>
    /// <remarks>
    /// The recorded change runs the reverse relocation, which records its own reverse, so the relocation undoes and
    /// redoes through the history. The change is recorded on the caller's synchronization context before this method
    /// completes. An undo that later changes made impossible is rejected like any request and changes nothing.
    /// </remarks>
    /// <param name="request">The requested moves.</param>
    /// <param name="label">What the history shows for the relocation, such as <c>Rename</c>.</param>
    /// <param name="history">The history the caller records the relocation in.</param>
    /// <param name="cancellationToken">Cancels before the first change.</param>
    /// <returns>The applied plan; it has no moves and records nothing when the request changes nothing.</returns>
    /// <exception cref="AssetRelocationException">The request was rejected or failed; no file is left changed.</exception>
    public Task<AssetRelocationPlan> RelocateAsync(AssetRelocationRequest request, string label, HistoryKeeper history, CancellationToken cancellationToken);

    /// <summary>Finds the authored files that reference an asset, or any asset inside a folder.</summary>
    /// <param name="virtualPath">The asset or folder.</param>
    /// <param name="cancellationToken">Cancels the scan.</param>
    /// <returns>The referrers' virtual paths.</returns>
    public Task<IReadOnlyList<string>> FindReferrersAsync(string virtualPath, CancellationToken cancellationToken);

    /// <summary>Copies assets or folders into a folder under unique names; references are not rewritten.</summary>
    /// <param name="sources">The virtual paths to copy.</param>
    /// <param name="targetFolder">The destination folder.</param>
    /// <param name="cancellationToken">Cancels before the first copy.</param>
    /// <returns>The copies' virtual paths.</returns>
    /// <exception cref="AssetRelocationException">The copy is not allowed; nothing was created.</exception>
    public Task<IReadOnlyList<string>> CopyAsync(IReadOnlyList<string> sources, string targetFolder, CancellationToken cancellationToken);

    /// <summary>Moves assets or folders, with their companion files, to the Recycle Bin.</summary>
    /// <param name="sources">The virtual paths to delete.</param>
    /// <param name="recycle">Moves absolute paths to the Recycle Bin.</param>
    /// <param name="cancellationToken">Cancels before deleting.</param>
    /// <returns>Completion after the files are recycled.</returns>
    /// <exception cref="AssetRelocationException">The delete is not allowed; nothing was deleted.</exception>
    public Task DeleteAsync(IReadOnlyList<string> sources, Func<IReadOnlyList<string>, Task> recycle, CancellationToken cancellationToken);

    /// <summary>Forgets cached references after content changed outside a relocation.</summary>
    public void Invalidate();
}
