// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.Mvvm.Messaging;
using DryIoc;
using Microsoft.UI.Dispatching;
using Oxygen.Editor.Assets;
using Oxygen.Editor.ContentBrowser.Messages;
using Oxygen.Editor.ContentPipeline.Relocation;

namespace Oxygen.Editor.World.SceneEditor;

/// <summary>
/// Keeps the open scene in step with Content Browser changes without reloading it: as a relocation participant it
/// re-points its affected references in memory, and it renames itself when the browser renames it. The runtime keeps
/// showing the re-pointed nodes as they were until the asset feed reports their new assets mounted, whoever cooks them.
/// </summary>
public partial class SceneEditorViewModel
{
    private readonly RelocatedRuntimeRefresh relocatedRuntime = new();
    private IDisposable? relocationParticipant;
    private IDisposable? relocatedContent;

    private string? SceneSourcePath => this.scene?.Project.ProjectInfo.Location is { } root
        ? Path.GetFullPath(Path.Combine(root, Oxygen.Editor.Projects.Constants.ContentFolderName, Oxygen.Editor.Projects.Constants.ScenesFolderName, this.scene.Name + Oxygen.Editor.Projects.Constants.SceneFileExtension))
        : null;

    private void RegisterRelocationMessages()
    {
        if (this.container.Resolve<IAssetRelocationService>(IfUnresolved.ReturnDefault) is { } relocation)
        {
            this.relocationParticipant = relocation.AddParticipant(new RelocationParticipant(this, DispatcherQueue.GetForCurrentThread()));
        }
        else
        {
            this.LogRelocationUnavailable(this.Metadata.Title);
        }

        this.messenger.Register<SceneAssetCommandMessage>(this, (_, message) =>
        {
            if (message.Command == SceneAssetCommand.Rename && !message.HasReceivedResponse && this.scene is { } current
                && string.Equals(current.Name, message.SceneName, StringComparison.OrdinalIgnoreCase) && message.NewName is { } name)
            {
                message.Reply(this.RenameOpenSceneAsync(name));
            }
        });
    }

    private void DisposeRelocationParticipant()
    {
        this.relocationParticipant?.Dispose();
        this.relocationParticipant = null;
        this.relocatedContent?.Dispose();
        this.relocatedContent = null;
        this.relocatedRuntime.Clear();
    }

    private async Task FollowRelocationAsync(AssetRelocationChange change)
    {
        if (this.isDisposed || this.scene is null || this.SceneSourcePath is not { } path)
        {
            this.LogRelocationSkipped(this.Metadata.Title, "the scene is not loaded");
            return;
        }

        if (change.FindRewrite(path) is not { } rewrite)
        {
            this.LogRelocationSkipped(this.scene.Name, "the relocation did not rewrite " + path);
            return;
        }

        // The command service logs the outcome, including why the scene could not follow.
        var result = await this.commandService.FollowRelocationAsync(this.CreateCommandContext(), change, rewrite).ConfigureAwait(true);
        if (result.Succeeded && (result.RuntimeNodes.Count != 0 || result.RuntimeEnvironment))
        {
            this.relocatedRuntime.Add(result);
            this.LogRelocatedContentAwaited(this.Metadata.Title, result.RuntimeNodes.Count, result.RuntimeEnvironment);
            this.ObserveRelocatedContent();
        }
    }

    // The feed publishes its current entries on subscription, so assets that are already mounted show at once.
    private void ObserveRelocatedContent()
    {
        if (this.relocatedContent is not null)
        {
            return;
        }

        if (this.container.Resolve<IAssetFeed>(IfUnresolved.ReturnDefault) is not { } feed)
        {
            this.LogRelocatedContentUnobservable(this.Metadata.Title);
            this.relocatedRuntime.Clear();
            return;
        }

        var dispatcher = DispatcherQueue.GetForCurrentThread();
        this.relocatedContent = feed.Items.Subscribe(entries =>
        {
            if (dispatcher is null)
            {
                _ = this.ShowMountedRelocationsAsync(entries);
            }
            else
            {
                _ = dispatcher.TryEnqueue(() => _ = this.ShowMountedRelocationsAsync(entries));
            }
        });
    }

    private async Task ShowMountedRelocationsAsync(IReadOnlyList<AssetEntry> entries)
    {
        if (this.isDisposed || this.scene is not { } current || !this.relocatedRuntime.IsPending)
        {
            return;
        }

        var ready = this.relocatedRuntime.TakeReady(current, uri => entries.Find(uri)?.RuntimeAvailability
            is AssetRuntimeAvailability.Mounted or AssetRuntimeAvailability.NotApplicable);
        if (!this.relocatedRuntime.IsPending)
        {
            this.relocatedContent?.Dispose();
            this.relocatedContent = null;
        }

        if (ready.Nodes.Count != 0 || ready.Environment)
        {
            await this.commandService.RefreshRelocatedRuntimeAsync(this.CreateCommandContext(), ready.Nodes, ready.Environment).ConfigureAwait(true);
        }
    }

    private async Task<string?> RenameOpenSceneAsync(string name)
    {
        var previous = this.scene?.Name ?? string.Empty;
        var result = await this.commandService.RenameSceneAsync(this.CreateCommandContext(), name).ConfigureAwait(true);
        if (result.Succeeded)
        {
            this.LogOpenSceneRenamed(previous, name);
            return null;
        }

        this.LogOpenSceneNotRenamed(previous, name);
        return $"Scene '{previous}' could not be renamed to '{name}'. The scene editor's result shows why.";
    }

    /// <summary>Runs the scene's follow-up on its UI thread; the relocation calls participants from a worker.</summary>
    private sealed class RelocationParticipant(SceneEditorViewModel owner, DispatcherQueue? dispatcher) : IAssetRelocationParticipant
    {
        [System.Diagnostics.CodeAnalysis.SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "the failure is reported through the operation result or forwarded to the awaiting caller and must not escape")]
        public Task FollowAsync(AssetRelocationChange change)
        {
            if (dispatcher?.HasThreadAccess != false)
            {
                return owner.FollowRelocationAsync(change);
            }

            var completion = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
            if (!dispatcher.TryEnqueue(() => _ = this.FollowAndCompleteAsync(change, completion)))
            {
                completion.SetException(new InvalidOperationException("The scene editor's thread is no longer available."));
            }

            return completion.Task;
        }

        [System.Diagnostics.CodeAnalysis.SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "the failure is forwarded to the awaiting caller and must not escape")]
        private async Task FollowAndCompleteAsync(AssetRelocationChange change, TaskCompletionSource completion)
        {
            try
            {
                await owner.FollowRelocationAsync(change).ConfigureAwait(true);
                completion.SetResult();
            }
            catch (Exception failure)
            {
                completion.SetException(failure);
            }
        }
    }
}
