// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.ContentBrowser.Messages;
using Oxygen.Editor.ContentPipeline.Relocation;

namespace Oxygen.Editor.ContentBrowser.Relocation;

/// <summary>
/// The Content Browser's rename, move, copy and delete workflow: it runs the change, keeps the browser, open
/// documents and cooked content current, and offers Undo for the last rename or move. Only a delete of content that
/// other content uses asks for confirmation.
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

    /// <summary>Runs a rename or move.</summary>
    /// <param name="request">The requested moves.</param>
    /// <param name="title">The operation title for results.</param>
    /// <returns>The outcome, or null when the request changes nothing.</returns>
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
    public Task<AssetRelocationOutcome> CopyAsync(IReadOnlyList<string> sources, string targetFolder, string title);

    /// <summary>Confirms when other content uses the items, then moves them to the Recycle Bin.</summary>
    /// <param name="sources">The virtual paths to delete.</param>
    /// <returns>The outcome, or null when the user cancelled.</returns>
    public Task<AssetRelocationOutcome?> DeleteAsync(IReadOnlyList<string> sources);

    /// <summary>Asks the scene workspace to rename, duplicate or delete a scene.</summary>
    /// <param name="command">The change.</param>
    /// <param name="sceneName">The scene's name.</param>
    /// <param name="newName">The new name for a rename or duplicate.</param>
    /// <returns>The outcome.</returns>
    public Task<AssetRelocationOutcome> RunSceneCommandAsync(SceneAssetCommand command, string sceneName, string? newName);

    /// <summary>Finds the authored files that use an asset or any asset inside a folder.</summary>
    /// <param name="virtualPath">The asset or folder.</param>
    /// <param name="cancellationToken">Cancels the lookup.</param>
    /// <returns>The referrers' virtual paths.</returns>
    public Task<IReadOnlyList<string>> FindReferrersAsync(string virtualPath, CancellationToken cancellationToken);

    /// <summary>Asks for a new name.</summary>
    /// <param name="title">The dialog title.</param>
    /// <param name="initial">The current name.</param>
    /// <param name="allowSeparators">Whether a nested path such as <c>Vehicles/Car</c> is allowed.</param>
    /// <returns>The accepted name, or null when cancelled or unchanged.</returns>
    public Task<string?> PromptNameAsync(string title, string initial, bool allowSeparators = false);
}

/// <summary>The result of a Content Browser change.</summary>
/// <param name="Succeeded">Whether the change committed.</param>
/// <param name="Title">The operation title.</param>
/// <param name="Message">What changed, or why it failed.</param>
public sealed record AssetRelocationOutcome(bool Succeeded, string Title, string Message);
