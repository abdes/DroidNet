// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Globalization;
using System.Text;
using DroidNet.Aura.Dialogs;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Oxygen.Editor.ContentPipeline;
using Oxygen.Editor.ContentPipeline.Relocation;
using Oxygen.Editor.Documents;
using Oxygen.Editor.Shell.Desktop;
using Oxygen.Editor.Shell.Documents;
using Oxygen.Managed.Assets;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.ContentExplorer.Relocation;

/// <summary>Runs Content Explorer renames, moves, copies and deletes, and reports their outcomes.</summary>
/// <remarks>
/// Renames and moves, of scenes as of any asset, are recorded in the session's global history, so they undo and redo in
/// order with everything else recorded there; copies and deletes are not recorded and leave the history as it is. The
/// relocation service reports every file it changed, and the asset feed, the sources tree and open documents follow
/// that report, whoever asked for the change. A rename or move first asks to save every unsaved document, since open
/// documents follow it from their saved files; an undo or redo is refused while one remains.
/// </remarks>
/// <param name="relocation">Applies the file changes.</param>
/// <param name="histories">Holds the global history that renames and moves are recorded in.</param>
/// <param name="documents">The window's document area, which saves the unsaved documents before a rename or move after asking.</param>
/// <param name="openDocuments">The open documents, which a delete names when it closes them.</param>
/// <param name="dialogs">Asks for names, and confirms deleting content that other content uses or that is open.</param>
/// <param name="shell">Moves deleted files to the Recycle Bin.</param>
/// <param name="logger">Records requests and outcomes.</param>
public sealed partial class AssetRelocationWorkflow(
    IAssetRelocationService relocation,
    IHistoryService histories,
    IDocumentArea documents,
    OpenDocuments openDocuments,
    IDialogService dialogs,
    IDesktopIntegration shell,
    ILogger<AssetRelocationWorkflow>? logger = null) : IAssetRelocationWorkflow, IDisposable
{
    private const int ListedItems = 8;

    private readonly ILogger logger = (ILogger?)logger ?? NullLogger.Instance;
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
        var operation = OperationOf(request);
        if (!await documents.SaveBeforeAsync(operation, _ => Task.FromResult(relocation.FindUnsavedDocuments())).ConfigureAwait(true))
        {
            return new(Succeeded: false, title, operation == SaveBeforeOperation.Rename
                ? "Not renamed: the documents were not saved."
                : "Not moved: the documents were not saved.");
        }

        try
        {
            var applied = await relocation.RelocateAsync(request, title, this.history, CancellationToken.None).ConfigureAwait(true);
            if (applied.Moves.Count == 0 && applied.RewrittenFiles.Count == 0)
            {
                this.LogNothingToDo(title);
                return null;
            }

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
    public async Task<AssetRelocationOutcome> CopyAsync(IReadOnlyList<AssetPath> sources, AssetPath targetFolder, string title)
    {
        try
        {
            var copies = await relocation.CopyAsync(sources, targetFolder, CancellationToken.None).ConfigureAwait(true);
            var message = copies.Count == 1
                ? $"Created {copies[0].Name}."
                : string.Create(CultureInfo.CurrentCulture, $"Created {copies.Count} copies in {targetFolder}.");
            return this.Publish(new(Succeeded: true, title, message));
        }
        catch (AssetRelocationException error)
        {
            return this.Publish(new(Succeeded: false, title, error.Message));
        }
    }

    /// <inheritdoc/>
    public async Task<AssetRelocationOutcome?> PasteAsync(IReadOnlyList<AssetPath> sources, bool move, AssetPath targetFolder)
    {
        ArgumentNullException.ThrowIfNull(sources);
        ArgumentNullException.ThrowIfNull(targetFolder);
        if (!move)
        {
            return await this.CopyAsync(sources, targetFolder, "Paste").ConfigureAwait(true);
        }

        return relocation.CreatePasteRequest(sources, targetFolder) is { } request
            ? await this.RelocateAsync(request, "Move").ConfigureAwait(true)
            : null;
    }

    /// <inheritdoc/>
    public async Task<AssetRelocationOutcome?> DeleteAsync(IReadOnlyList<AssetPath> sources)
    {
        ArgumentNullException.ThrowIfNull(sources);
        const string title = "Delete";
        var referrers = await relocation.FindReferrersOutsideAsync(sources, CancellationToken.None).ConfigureAwait(true);
        var open = this.FindOpenDocuments(sources);
        if (referrers.Count != 0 || open.Count != 0)
        {
            var review = new StringBuilder();
            if (open.Count != 0)
            {
                _ = review.Append(CultureInfo.CurrentCulture, $"{string.Join(", ", open.Select(static document => document.Name))} {(open.Count == 1 ? "is" : "are")} open and will close.");
                if (open.Any(static document => document.IsDirty))
                {
                    _ = review.Append(CultureInfo.CurrentCulture, $" Unsaved changes to {string.Join(", ", open.Where(static document => document.IsDirty).Select(static document => document.Name))} are discarded.");
                }

                _ = review.AppendLine();
            }

            if (referrers.Count != 0)
            {
                _ = review.Append(CultureInfo.CurrentCulture, $"{Describe(sources)} {(sources.Count == 1 ? "is" : "are")} used by:").AppendLine();
                AppendList(review, referrers);
                _ = review.AppendLine().Append("Those references will show as missing. ");
            }

            _ = review.Append("The files go to the Recycle Bin.");
            if (!await dialogs.ConfirmAsync(title, review.ToString()).ConfigureAwait(true))
            {
                this.LogReviewCancelled(title, referrers.Count, open.Count);
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

        return this.Publish(new(Succeeded: true, title, $"Moved {Describe(sources)} to the Recycle Bin. Restore {(sources.Count == 1 ? "it" : "them")} from there to bring {(sources.Count == 1 ? "it" : "them")} back."));
    }

    /// <inheritdoc/>
    public void Dispose() => this.historyObserver.Dispose();

    /// <inheritdoc/>
    public Task<IReadOnlyList<AssetPath>> FindReferrersAsync(AssetPath virtualPath, CancellationToken cancellationToken)
        => relocation.FindReferrersAsync(virtualPath, cancellationToken);

    /// <inheritdoc/>
    [System.Diagnostics.CodeAnalysis.SuppressMessage("Design", "CA1508:Avoid dead conditional code", Justification = "the validator callback assigns the accepted name while the dialog is open")]
    public async Task<string?> PromptNameAsync(string title, string initial)
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
            PrimaryAction = _ =>
            {
                var value = VirtualPath.NormalizeTypedName(input.Text);
                var invalid = VirtualPath.ValidateSegment(value, VirtualPathRuleSet.Authored)?.ToMessage();
                error.Text = invalid ?? string.Empty;
                error.Visibility = invalid is null ? Visibility.Collapsed : Visibility.Visible;
                accepted = invalid is null ? value : null;
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
        var moved = plan.Request.Moves.Count;
        var subject = moved == 1
            ? $"'{plan.Request.Moves[0].Source.Name}' is now '{plan.Request.Moves[0].Target.Name}'"
            : string.Create(CultureInfo.CurrentCulture, $"Moved {moved} {(moved == 1 ? "item" : "items")}");
        return plan.Referrers.Count == 0
            ? subject + "."
            : string.Create(CultureInfo.CurrentCulture, $"{subject}; updated {plan.Referrers.Count} {(plan.Referrers.Count == 1 ? "file that uses it" : "files that use it")}.");
    }

    private static string Describe(IReadOnlyList<AssetPath> sources)
        => sources.Count == 1 ? $"'{sources[0].Name}'" : string.Create(CultureInfo.CurrentCulture, $"{sources.Count} items");

    private static void AppendList(StringBuilder text, IReadOnlyList<AssetPath> items)
    {
        foreach (var item in items.Take(ListedItems))
        {
            _ = text.Append("  • ").Append(item).AppendLine();
        }

        if (items.Count > ListedItems)
        {
            _ = text.Append(CultureInfo.CurrentCulture, $"  and {items.Count - ListedItems} more").AppendLine();
        }
    }

    // A request that keeps every asset in its folder renames; any other moves.
    private static SaveBeforeOperation OperationOf(AssetRelocationRequest request)
        => request.Moves.All(static move => move.Source.Parent == move.Target.Parent) ? SaveBeforeOperation.Rename : SaveBeforeOperation.Move;

    // The open documents whose files a delete of the sources removes, so the delete can name them.
    private IReadOnlyList<(string Name, bool IsDirty)> FindOpenDocuments(IReadOnlyList<AssetPath> sources)
    {
        var deleted = sources.Select(AssetFileTypes.ToIdentity).ToArray();
        return [.. openDocuments.Documents
            .Where(document => deleted.Any(document.File.Asset.IsSameOrInside))
            .Select(static document => (document.Title, document.IsDirty))
            .DistinctBy(static document => document.Title, StringComparer.OrdinalIgnoreCase)
            .OrderBy(static document => document.Title, StringComparer.OrdinalIgnoreCase)];
    }

    // The relocation service replays the history; a rejected undo or redo is reported under the entry it named.
    private async Task<AssetRelocationOutcome?> ReplayAsync(bool undo)
    {
        var title = undo ? "Undo" : "Redo";
        var named = undo ? this.UndoLabel : this.RedoLabel;
        string? label;
        try
        {
            label = await (undo ? relocation.UndoAsync(this.history, CancellationToken.None) : relocation.RedoAsync(this.history, CancellationToken.None)).ConfigureAwait(true);
        }
        catch (AssetRelocationException error)
        {
            return this.Publish(new(Succeeded: false, $"{title} {named}", error.Message));
        }

        if (label is null)
        {
            return null;
        }

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
