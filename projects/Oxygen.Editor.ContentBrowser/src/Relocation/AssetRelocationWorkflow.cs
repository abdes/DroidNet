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
using Oxygen.Editor.ContentBrowser.AssetIdentity;
using Oxygen.Editor.ContentBrowser.Messages;
using Oxygen.Editor.ContentPipeline;
using Oxygen.Editor.ContentPipeline.Relocation;

namespace Oxygen.Editor.ContentBrowser.Relocation;

/// <summary>Runs Content Browser renames, moves, copies and deletes, then refreshes the browser and open documents.</summary>
/// <param name="relocation">Applies the file changes and recooks after them.</param>
/// <param name="dialogs">Asks for names, and confirms deleting content that other content uses.</param>
/// <param name="messenger">Tells the browser and open documents what changed.</param>
/// <param name="assets">The browser's asset rows.</param>
/// <param name="pipeline">Recooks the project after a scene command.</param>
/// <param name="shell">Moves deleted files to the Recycle Bin.</param>
/// <param name="logger">Records requests, outcomes and the follow-up refresh and cook.</param>
public sealed partial class AssetRelocationWorkflow(
    IAssetRelocationService relocation,
    IDialogService dialogs,
    IMessenger messenger,
    IContentBrowserAssetProvider assets,
    IContentPipelineService pipeline,
    IAssetShell shell,
    ILogger<AssetRelocationWorkflow>? logger = null) : IAssetRelocationWorkflow, IDisposable
{
    private const int ListedItems = 8;

    private readonly ILogger logger = (ILogger?)logger ?? NullLogger.Instance;

    // Content changed outside a relocation (a save, an import, an external edit) makes cached referrers stale.
    private readonly IDisposable contentChanges = assets.Items.Subscribe(_ => relocation.Invalidate());
    private AssetRelocationRequest? undo;

    /// <inheritdoc/>
    public event EventHandler<AssetRelocationCompletedEventArgs>? Completed;

    /// <inheritdoc/>
    public bool CanUndo => this.undo is not null;

    /// <inheritdoc/>
    public async Task<AssetRelocationOutcome?> RelocateAsync(AssetRelocationRequest request, string title)
    {
        ArgumentNullException.ThrowIfNull(request);
        try
        {
            var applied = await relocation.RelocateAsync(request, CancellationToken.None).ConfigureAwait(true);
            if (applied.Moves.Count == 0 && applied.GroupFolders.Count == 0 && applied.RewrittenFiles.Count == 0)
            {
                this.LogNothingToDo(title);
                return null;
            }

            this.undo = applied.Reverse;
            await this.AfterChangeAsync(title, applied.Moves, applied.RewrittenFiles, [], cook: false).ConfigureAwait(true);
            _ = this.ReportPublicationAsync(title, applied.ContentPublished);
            return this.Publish(new(Succeeded: true, title, DescribeResult(applied), CanUndo: true));
        }
        catch (AssetRelocationException error)
        {
            return this.Publish(new(Succeeded: false, title, error.Message, this.CanUndo));
        }
    }

    /// <inheritdoc/>
    public async Task<AssetRelocationOutcome?> UndoAsync()
    {
        if (this.undo is not { } request)
        {
            return null;
        }

        this.LogUndoRequested();
        try
        {
            var applied = await relocation.RelocateAsync(request, CancellationToken.None).ConfigureAwait(true);
            this.undo = null;
            await this.AfterChangeAsync("Undo", applied.Moves, applied.RewrittenFiles, [], cook: false).ConfigureAwait(true);
            _ = this.ReportPublicationAsync("Undo", applied.ContentPublished);
            return this.Publish(new(Succeeded: true, "Undo", "Restored the previous names, locations and references.", CanUndo: false));
        }
        catch (AssetRelocationException error)
        {
            // A rejected or rolled-back undo changed nothing, so it stays available, for example after a save.
            return this.Publish(new(Succeeded: false, "Undo", error.Message, CanUndo: true));
        }
    }

    /// <inheritdoc/>
    public async Task<AssetRelocationOutcome> CopyAsync(IReadOnlyList<string> sources, string targetFolder, string title)
    {
        try
        {
            var copies = await relocation.CopyAsync(sources, targetFolder, CancellationToken.None).ConfigureAwait(true);
            await this.AfterChangeAsync(title, [], [], [], cook: false).ConfigureAwait(true);
            var message = copies.Count == 1
                ? $"Created {RelocationPaths.GetName(copies[0])}."
                : string.Create(CultureInfo.CurrentCulture, $"Created {copies.Count} copies in {targetFolder}.");
            this.ForgetUndo();
            return this.Publish(new(Succeeded: true, title, message, CanUndo: false));
        }
        catch (AssetRelocationException error)
        {
            return this.Publish(new(Succeeded: false, title, error.Message, this.CanUndo));
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

        var deleted = new List<string>();
        var foldersDeleted = false;
        try
        {
            await relocation.DeleteAsync(
                sources,
                paths =>
                {
                    foldersDeleted = paths.Any(Directory.Exists);
                    shell.MoveToRecycleBin(paths);
                    deleted.AddRange(paths);
                    return Task.CompletedTask;
                },
                CancellationToken.None).ConfigureAwait(true);
        }
        catch (Exception error) when (error is AssetRelocationException or IOException)
        {
            return this.Publish(new(Succeeded: false, title, error.Message, this.CanUndo));
        }

        await this.AfterChangeAsync(title, [], [], deleted, cook: false, foldersDeleted).ConfigureAwait(true);
        this.ForgetUndo();
        return this.Publish(new(Succeeded: true, title, $"Moved {Describe(sources)} to the Recycle Bin. Restore {(sources.Count == 1 ? "it" : "them")} from there to bring {(sources.Count == 1 ? "it" : "them")} back.", CanUndo: false));
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
        var deleted = new List<string>();
        var request = new SceneAssetCommandMessage(command, sceneName, newName, paths =>
        {
            shell.MoveToRecycleBin(paths);
            deleted.AddRange(paths);
            return Task.CompletedTask;
        });
        this.LogSceneCommand(command, sceneName, newName);
        _ = messenger.Send(request);
        if (!request.HasReceivedResponse)
        {
            this.LogSceneCommandUnhandled(command, sceneName);
            return this.Publish(new(Succeeded: false, title, "Scenes change only while the project's world editor is open.", this.CanUndo));
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
            return this.Publish(new(Succeeded: false, title, error, this.CanUndo));
        }

        relocation.Invalidate();
        await this.AfterChangeAsync(title, [], [], deleted, cook: true).ConfigureAwait(true);
        var message = command switch
        {
            SceneAssetCommand.Rename => $"Renamed scene '{sceneName}' to '{newName}'.",
            SceneAssetCommand.Duplicate => $"Duplicated scene '{sceneName}'.",
            _ => $"Moved scene '{sceneName}' to the Recycle Bin.",
        };
        this.ForgetUndo();
        return this.Publish(new(Succeeded: true, title, message, CanUndo: false));
    }

    /// <inheritdoc/>
    public void Dispose() => this.contentChanges.Dispose();

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
    private void ForgetUndo()
    {
        if (this.undo is not null)
        {
            this.undo = null;
            this.LogUndoForgotten();
        }
    }

    // The service, the scene workspace or this workflow logged the cause at its level; this records what the user saw.
    private AssetRelocationOutcome Publish(AssetRelocationOutcome outcome)
    {
        this.LogOutcome(outcome.Title, outcome.Succeeded, outcome.Message);
        this.Completed?.Invoke(this, new(outcome));
        return outcome;
    }

    // The files and references are committed; if republishing fails, the browser still shows the old cooked asset
    // until the next successful cook, so say so instead of leaving the success message.
    private async Task ReportPublicationAsync(string title, Task<string?> contentPublished)
    {
        if (await contentPublished.ConfigureAwait(true) is { } problem)
        {
            _ = this.Publish(new(Succeeded: false, title, "The files and references were updated, but republishing the cooked content failed, so the old asset may still be listed. " + problem, this.CanUndo));
        }
    }

    // Refreshes rows and pickers and tells the sources tree and open documents what changed. Relocations, copies and
    // deletes recook through the relocation service, after open documents followed; a scene command changes files
    // through the scene workspace, so it starts the cook here.
    private async Task AfterChangeAsync(string title, IReadOnlyList<RelocationFileMove> moves, IReadOnlyList<string> rewritten, List<string> deleted, bool cook, bool foldersDeleted = false)
    {
        relocation.Invalidate();
        await assets.RefreshAsync(AssetBrowserFilter.Default).ConfigureAwait(true);
        this.LogAnnouncingChanges(title, moves.Count, rewritten.Count, deleted.Count);
        _ = messenger.Send(new AssetsChangedMessage());
        _ = messenger.Send(new AssetFilesChangedMessage(moves, rewritten, deleted) { FoldersChanged = foldersDeleted || moves.Any(static move => move.IsDirectory) });
        if (cook)
        {
            this.LogCookStarted(title);
            _ = this.CookAfterChangeAsync(title);
        }
    }

    [System.Diagnostics.CodeAnalysis.SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "The follow-up cook runs after the change committed; its failure is logged and shown in the Cooking panel.")]
    private async Task CookAfterChangeAsync(string title)
    {
        try
        {
            var result = await pipeline.CookProjectAsync(CancellationToken.None).ConfigureAwait(false);
            if (result.Status is Oxygen.Managed.Core.Diagnostics.OperationStatus.Succeeded or Oxygen.Managed.Core.Diagnostics.OperationStatus.SucceededWithWarnings)
            {
                this.LogCookPublished(title, result.Status);
            }
            else
            {
                this.LogCookNotSucceeded(title, result.Status, string.Join(" ", result.Diagnostics.Select(static diagnostic => diagnostic.Message)));
            }
        }
        catch (OperationCanceledException)
        {
            this.LogCookCancelled(title);
        }
        catch (Exception failure)
        {
            this.LogCookFailed(failure, title);
        }
    }
}
