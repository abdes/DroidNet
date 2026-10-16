// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Globalization;
using System.Text;
using CommunityToolkit.Mvvm.Messaging;
using DroidNet.Aura.Dialogs;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Oxygen.Editor.Assets;
using Oxygen.Editor.ContentBrowser.Messages;
using Oxygen.Editor.ContentPipeline;
using Oxygen.Editor.ContentPipeline.Relocation;
using Oxygen.Editor.Shell.Documents;

namespace Oxygen.Editor.ContentBrowser.Relocation;

/// <summary>Runs Content Browser renames, moves, copies and deletes, then refreshes the browser and open documents.</summary>
/// <remarks>
/// Renames and moves are recorded in the session's global history, so they undo and redo in order with everything
/// else recorded there; copies, deletes and scene commands are not recorded and leave the history as it is.
/// </remarks>
/// <param name="relocation">Applies the file changes.</param>
/// <param name="histories">Holds the global history that renames and moves are recorded in.</param>
/// <param name="dialogs">Asks for names, and confirms deleting content that other content uses.</param>
/// <param name="messenger">Hands scene commands to the open scene workspace.</param>
/// <param name="assets">The browser's asset rows.</param>
/// <param name="shell">Moves deleted files to the Recycle Bin.</param>
/// <param name="logger">Records requests and outcomes.</param>
public sealed partial class AssetRelocationWorkflow(
    IAssetRelocationService relocation,
    IHistoryService histories,
    IDialogService dialogs,
    IMessenger messenger,
    IAssetFeed assets,
    IAssetShell shell,
    ILogger<AssetRelocationWorkflow>? logger = null) : IAssetRelocationWorkflow, IDisposable
{
    private const int ListedItems = 8;

    private readonly ILogger logger = (ILogger?)logger ?? NullLogger.Instance;

    // Content changed outside a relocation (a save, an import, an external edit) makes cached referrers stale.
    private readonly IDisposable contentChanges = assets.Items.Subscribe(_ => relocation.Invalidate());
    private readonly DroidNet.TimeMachine.HistoryKeeper history = histories.GlobalHistory;
    private readonly HistoryObserver historyObserver = new(histories.GlobalHistory);

    /// <inheritdoc/>
    public event EventHandler<AssetRelocationCompletedEventArgs>? Completed;

    /// <inheritdoc/>
    public event EventHandler? HistoryChanged
    {
        add => this.historyObserver.Changed += value;
        remove => this.historyObserver.Changed -= value;
    }

    /// <inheritdoc/>
    public bool CanUndo => this.history.CanUndo;

    /// <inheritdoc/>
    public bool CanRedo => this.history.CanRedo;

    /// <inheritdoc/>
    public string? UndoLabel => this.history.CanUndo ? this.history.UndoStack[^1].Key.ToString() : null;

    /// <inheritdoc/>
    public string? RedoLabel => this.history.CanRedo ? this.history.RedoStack[0].Key.ToString() : null;

    /// <inheritdoc/>
    public async Task<AssetRelocationOutcome?> RelocateAsync(AssetRelocationRequest request, string title)
    {
        ArgumentNullException.ThrowIfNull(request);
        try
        {
            var applied = await relocation.RelocateAsync(request, title, this.history, CancellationToken.None).ConfigureAwait(true);
            if (applied.Moves.Count == 0 && applied.GroupFolders.Count == 0 && applied.RewrittenFiles.Count == 0)
            {
                this.LogNothingToDo(title);
                return null;
            }

            await this.AfterChangeAsync().ConfigureAwait(true);
            return this.Publish(new(Succeeded: true, title, DescribeResult(applied)));
        }
        catch (AssetRelocationException error)
        {
            return this.Publish(new(Succeeded: false, title, error.Message));
        }
    }

    /// <inheritdoc/>
    public Task<AssetRelocationOutcome?> UndoAsync()
        => this.ReplayAsync(undo: true);

    /// <inheritdoc/>
    public Task<AssetRelocationOutcome?> RedoAsync()
        => this.ReplayAsync(undo: false);

    /// <inheritdoc/>
    public async Task<AssetRelocationOutcome> CopyAsync(IReadOnlyList<string> sources, string targetFolder, string title)
    {
        try
        {
            var copies = await relocation.CopyAsync(sources, targetFolder, CancellationToken.None).ConfigureAwait(true);
            await this.AfterChangeAsync().ConfigureAwait(true);
            var message = copies.Count == 1
                ? $"Created {RelocationPaths.GetName(copies[0])}."
                : string.Create(CultureInfo.CurrentCulture, $"Created {copies.Count} copies in {targetFolder}.");
            return this.Publish(new(Succeeded: true, title, message));
        }
        catch (AssetRelocationException error)
        {
            return this.Publish(new(Succeeded: false, title, error.Message));
        }
    }

    /// <inheritdoc/>
    public async Task<AssetRelocationOutcome?> DeleteAsync(IReadOnlyList<string> sources)
    {
        ArgumentNullException.ThrowIfNull(sources);
        const string title = "Delete";
        relocation.Invalidate();
        var referrers = new SortedSet<string>(StringComparer.OrdinalIgnoreCase);
        foreach (var source in sources)
        {
            referrers.UnionWith((await relocation.FindReferrersAsync(source, CancellationToken.None).ConfigureAwait(true))
                .Where(referrer => !sources.Any(deleted => RelocationPaths.IsSameOrInside(referrer, deleted))));
        }

        if (referrers.Count != 0)
        {
            var review = new StringBuilder()
                .Append(CultureInfo.CurrentCulture, $"{Describe(sources)} {(sources.Count == 1 ? "is" : "are")} used by:").AppendLine();
            AppendList(review, referrers.ToArray());
            _ = review.AppendLine().Append("Those references will show as missing. The files go to the Recycle Bin.");
            if (!await dialogs.ConfirmAsync(title, review.ToString()).ConfigureAwait(true))
            {
                this.LogReviewCancelled(title, referrers.Count);
                return null;
            }
        }

        try
        {
            await relocation.DeleteAsync(
                sources,
                paths =>
                {
                    shell.MoveToRecycleBin(paths);
                    return Task.CompletedTask;
                },
                CancellationToken.None).ConfigureAwait(true);
        }
        catch (Exception error) when (error is AssetRelocationException or IOException)
        {
            return this.Publish(new(Succeeded: false, title, error.Message));
        }

        await this.AfterChangeAsync().ConfigureAwait(true);
        return this.Publish(new(Succeeded: true, title, $"Moved {Describe(sources)} to the Recycle Bin. Restore {(sources.Count == 1 ? "it" : "them")} from there to bring {(sources.Count == 1 ? "it" : "them")} back."));
    }

    /// <inheritdoc/>
    public async Task<AssetRelocationOutcome> RunSceneCommandAsync(SceneAssetCommand command, string sceneName, string? newName)
    {
        var title = command switch
        {
            SceneAssetCommand.Rename => "Rename scene",
            SceneAssetCommand.Duplicate => "Duplicate scene",
            _ => "Delete scene",
        };
        var request = new SceneAssetCommandMessage(command, sceneName, newName, paths =>
        {
            shell.MoveToRecycleBin(paths);
            return Task.CompletedTask;
        });
        this.LogSceneCommand(command, sceneName, newName);
        _ = messenger.Send(request);
        if (!request.HasReceivedResponse)
        {
            this.LogSceneCommandUnhandled(command, sceneName);
            return this.Publish(new(Succeeded: false, title, "Scenes change only while the project's world editor is open."));
        }

        string? error;
        try
        {
            error = await request.Response.ConfigureAwait(true);
        }
        catch (Exception failure) when (failure is IOException or UnauthorizedAccessException or InvalidOperationException)
        {
            this.LogSceneCommandFailed(failure, command, sceneName);
            error = failure.Message;
        }

        if (error is not null)
        {
            return this.Publish(new(Succeeded: false, title, error));
        }

        await this.AfterChangeAsync().ConfigureAwait(true);
        var message = command switch
        {
            SceneAssetCommand.Rename => $"Renamed scene '{sceneName}' to '{newName}'.",
            SceneAssetCommand.Duplicate => $"Duplicated scene '{sceneName}'.",
            _ => $"Moved scene '{sceneName}' to the Recycle Bin.",
        };
        return this.Publish(new(Succeeded: true, title, message));
    }

    /// <inheritdoc/>
    public void Dispose()
    {
        this.contentChanges.Dispose();
        this.historyObserver.Dispose();
    }

    /// <inheritdoc/>
    public Task<IReadOnlyList<string>> FindReferrersAsync(string virtualPath, CancellationToken cancellationToken)
        => relocation.FindReferrersAsync(virtualPath, cancellationToken);

    /// <inheritdoc/>
    [System.Diagnostics.CodeAnalysis.SuppressMessage("Design", "CA1508:Avoid dead conditional code", Justification = "the validator callback assigns the accepted name while the dialog is open")]
    public async Task<string?> PromptNameAsync(string title, string initial, bool allowSeparators = false)
    {
        ArgumentNullException.ThrowIfNull(initial);
        var input = new TextBox { Text = initial, SelectionStart = 0, SelectionLength = initial.Length, MinWidth = 320 };
        var error = new TextBlock { TextWrapping = TextWrapping.Wrap, Visibility = Visibility.Collapsed };
        error.Foreground = Application.Current.Resources.TryGetValue("SystemFillColorCriticalBrush", out var brush) ? brush as Microsoft.UI.Xaml.Media.Brush : null;
        var content = new StackPanel { Spacing = 8, Children = { input, error } };
        input.Loaded += (_, _) => _ = input.Focus(FocusState.Programmatic);
        string? accepted = null;
        var spec = new DialogSpec(title, content)
        {
            PrimaryButtonText = "Rename",
            CloseButtonText = "Cancel",
            DefaultButton = DialogButton.Primary,
            PrimaryAction = () =>
            {
                var value = input.Text.Trim();
                var segments = allowSeparators ? value.Replace('\\', '/').Split('/') : [value];
                var invalid = segments.Select(RelocationPaths.ValidateName).FirstOrDefault(static reason => reason is not null);
                error.Text = invalid ?? string.Empty;
                error.Visibility = invalid is null ? Visibility.Collapsed : Visibility.Visible;
                accepted = invalid is null ? string.Join('/', segments) : null;
                return Task.FromResult(invalid is null);
            },
        };
        return await dialogs.ShowAsync(spec).ConfigureAwait(true) == DialogButton.Primary
            && accepted is not null && !string.Equals(accepted, initial, StringComparison.Ordinal)
            ? accepted
            : null;
    }

    private static string DescribeResult(AssetRelocationPlan plan)
    {
        var moved = plan.Request.Moves.Count + plan.Request.GroupMoves.Count;
        var subject = plan.Request.Moves.Count == 1 && plan.Request.GroupMoves.Count == 0
            ? $"'{RelocationPaths.GetName(plan.Request.Moves[0].SourcePath)}' is now '{RelocationPaths.GetName(plan.Request.Moves[0].TargetPath)}'"
            : string.Create(CultureInfo.CurrentCulture, $"Moved {moved} {(moved == 1 ? "item" : "items")}");
        var groups = plan.GroupFolders.Count == 0 ? string.Empty
            : string.Create(CultureInfo.CurrentCulture, $" The model's output group moved in {plan.GroupFolders.Count} type folders.");
        return (plan.Referrers.Count == 0
            ? subject + "."
            : string.Create(CultureInfo.CurrentCulture, $"{subject}; updated {plan.Referrers.Count} {(plan.Referrers.Count == 1 ? "file that uses it" : "files that use it")}.")) + groups;
    }

    private static string Describe(IReadOnlyList<string> sources)
        => sources.Count == 1 ? $"'{RelocationPaths.GetName(sources[0])}'" : string.Create(CultureInfo.CurrentCulture, $"{sources.Count} items");

    private static void AppendList(StringBuilder text, string[] items)
    {
        foreach (var item in items.Take(ListedItems))
        {
            _ = text.Append("  • ").Append(item).AppendLine();
        }

        if (items.Length > ListedItems)
        {
            _ = text.Append(CultureInfo.CurrentCulture, $"  and {items.Length - ListedItems} more").AppendLine();
        }
    }

    // Undo reverses only the change just made: after a copy, delete or scene change, reversing an older rename would
    // undo something the user no longer sees as the last step.
    // A rejected or rolled-back undo or redo changed nothing, so its entry stays in the history.
    private async Task<AssetRelocationOutcome?> ReplayAsync(bool undo)
    {
        var label = undo ? this.UndoLabel : this.RedoLabel;
        if (label is null)
        {
            return null;
        }

        var title = undo ? "Undo" : "Redo";
        this.LogReplayRequested(title, label);
        try
        {
            if (undo)
            {
                await this.history.UndoAsync(CancellationToken.None).ConfigureAwait(true);
            }
            else
            {
                await this.history.RedoAsync(CancellationToken.None).ConfigureAwait(true);
            }
        }
        catch (AssetRelocationException error)
        {
            return this.Publish(new(Succeeded: false, $"{title} {label}", error.Message));
        }

        await this.AfterChangeAsync().ConfigureAwait(true);
        var message = undo ? "Restored the previous names, locations and references." : "Applied the names, locations and references again.";
        return this.Publish(new(Succeeded: true, $"{title} {label}", message));
    }

    // The service, the scene workspace or this workflow logged the cause at its level; this records what the user saw.
    private AssetRelocationOutcome Publish(AssetRelocationOutcome outcome)
    {
        this.LogOutcome(outcome.Title, outcome.Succeeded, outcome.Message);
        this.Completed?.Invoke(this, new(outcome));
        return outcome;
    }

    // Refreshes rows and pickers at once; the sources tree and open documents follow the relocation service's report,
    // and the changed files reach cook on change through the asset catalog.
    private async Task AfterChangeAsync()
    {
        relocation.Invalidate();
        await assets.RefreshAsync().ConfigureAwait(true);
    }

    /// <summary>Forwards the changes of a history's undo and redo entries until disposed.</summary>
    private sealed class HistoryObserver : IDisposable
    {
        private readonly System.Collections.Specialized.INotifyCollectionChanged undo;
        private readonly System.Collections.Specialized.INotifyCollectionChanged redo;

        public HistoryObserver(DroidNet.TimeMachine.HistoryKeeper history)
        {
            this.undo = history.UndoStack;
            this.redo = history.RedoStack;
            this.undo.CollectionChanged += this.OnChanged;
            this.redo.CollectionChanged += this.OnChanged;
        }

        public event EventHandler? Changed;

        public void Dispose()
        {
            this.undo.CollectionChanged -= this.OnChanged;
            this.redo.CollectionChanged -= this.OnChanged;
        }

        private void OnChanged(object? sender, System.Collections.Specialized.NotifyCollectionChangedEventArgs args)
            => this.Changed?.Invoke(this, EventArgs.Empty);
    }
}
