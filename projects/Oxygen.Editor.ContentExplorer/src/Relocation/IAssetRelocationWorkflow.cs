// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.ContentPipeline.Relocation;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.ContentExplorer.Relocation;

/// <summary>
/// The Content Explorer's rename, move, copy and delete workflow: it asks for names and confirmations, has the content
/// pipeline make the change and reports the outcome; the asset feed and the sources tree follow the pipeline's report
/// of the changed files. Undo and Redo run the session's global history. Only a delete of content that other content
/// uses asks for confirmation.
/// </summary>
public interface IAssetRelocationWorkflow
{
    /// <summary>Occurs when a change completed or failed, with the result to show.</summary>
    /// <remarks>Every operation also returns its outcome; a cancelled delete or a change of nothing raises nothing.</remarks>
    public event EventHandler<AssetRelocationCompletedEventArgs>? Completed;

    /// <summary>Occurs when the global history gains, loses, undoes or redoes an entry.</summary>
    public event EventHandler? HistoryChanged;

    /// <summary>Gets a value indicating whether the global history has an entry to undo.</summary>
    public bool CanUndo { get; }

    /// <summary>Gets a value indicating whether the global history has an entry to redo.</summary>
    public bool CanRedo { get; }

    /// <summary>Gets what the next undo undoes, such as <c>Rename</c>, or <see langword="null"/> when there is nothing.</summary>
    public string? UndoLabel { get; }

    /// <summary>Gets what the next redo redoes, or <see langword="null"/> when there is nothing.</summary>
    public string? RedoLabel { get; }

    /// <summary>Runs a rename or move, after asking to save the unsaved documents.</summary>
    /// <param name="request">The requested moves.</param>
    /// <param name="title">The operation title for results.</param>
    /// <returns>
    /// The outcome, or null when the request changes nothing. When the documents were not saved nothing runs, and the
    /// failed outcome is not reported, since the user chose it.
    /// </returns>
    public Task<AssetRelocationOutcome?> RelocateAsync(AssetRelocationRequest request, string title);

    /// <summary>Undoes the latest entry of the global history.</summary>
    /// <returns>The outcome, or null when there is nothing to undo.</returns>
    public Task<AssetRelocationOutcome?> UndoAsync();

    /// <summary>Redoes the latest undone entry of the global history.</summary>
    /// <returns>The outcome, or null when there is nothing to redo.</returns>
    public Task<AssetRelocationOutcome?> RedoAsync();

    /// <summary>Copies assets or folders into a folder under unique names.</summary>
    /// <param name="sources">The virtual paths to copy.</param>
    /// <param name="targetFolder">The destination folder.</param>
    /// <param name="title">The operation title.</param>
    /// <returns>The outcome.</returns>
    public Task<AssetRelocationOutcome> CopyAsync(IReadOnlyList<AssetPath> sources, AssetPath targetFolder, string title);

    /// <summary>Pastes cut or copied items into a folder: a cut moves them there, a copy creates copies.</summary>
    /// <param name="sources">The virtual paths on the clipboard.</param>
    /// <param name="move">Whether the items were cut.</param>
    /// <param name="targetFolder">The folder to paste into.</param>
    /// <returns>The outcome, or null when every cut item is already in that folder.</returns>
    public Task<AssetRelocationOutcome?> PasteAsync(IReadOnlyList<AssetPath> sources, bool move, AssetPath targetFolder);

    /// <summary>Confirms when other content uses the items or they are open, then moves them to the Recycle Bin.</summary>
    /// <param name="sources">The virtual paths to delete.</param>
    /// <returns>The outcome, or null when the user cancelled.</returns>
    public Task<AssetRelocationOutcome?> DeleteAsync(IReadOnlyList<AssetPath> sources);

    /// <summary>Finds the authored files that use an asset or any asset inside a folder.</summary>
    /// <param name="virtualPath">The asset or folder.</param>
    /// <param name="cancellationToken">Cancels the lookup.</param>
    /// <returns>The referrers' virtual paths.</returns>
    public Task<IReadOnlyList<AssetPath>> FindReferrersAsync(AssetPath virtualPath, CancellationToken cancellationToken);

    /// <summary>Asks for a new name.</summary>
    /// <param name="title">The dialog title.</param>
    /// <param name="initial">The current name.</param>
    /// <returns>The accepted name, or null when cancelled or unchanged.</returns>
    public Task<string?> PromptNameAsync(string title, string initial);
}

/// <summary>The result of a Content Explorer change.</summary>
/// <param name="Succeeded">Whether the change committed.</param>
/// <param name="Title">The operation title.</param>
/// <param name="Message">What changed, or why it failed.</param>
public sealed record AssetRelocationOutcome(bool Succeeded, string Title, string Message);
