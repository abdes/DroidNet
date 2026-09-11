// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.Mvvm.Messaging;
using Oxygen.Editor.ContentBrowser.Messages;
using Oxygen.Editor.Projects;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Documents;
using Oxygen.Managed.Core.Diagnostics;

#pragma warning disable IDE0130 // Authoring commands use the established WorldEditor namespace across this assembly.

namespace Oxygen.Editor.WorldEditor.Documents.Commands;

/// <summary>Accepts a confirmed source reload only for the current, quiescent authoring lifetime.</summary>
public sealed partial class SceneDocumentCommandService
{
    /// <inheritdoc/>
    [System.Diagnostics.CodeAnalysis.SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "The reload boundary reports failures to the initiating dialog while preserving document ownership.")]
    public async Task<SceneValueCommandResult<Scene>> ReloadSceneAsync(SceneDocumentCommandContext context, CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(context);
        Scene? accepted = null;
        try
        {
            await this.CompleteEditSessionsAsync(context, commit: true).ConfigureAwait(true);
            using var replacement = await SceneAuthoringGate.BeginReplacementAsync(context.Scene, cancellationToken).ConfigureAwait(true);
            if (replacement is null || !this.CanReloadScene(context))
            {
                return this.ReloadFailure(context, "The scene is no longer current or its history is still changing. Try Reload again when the operation finishes.");
            }

            var version = context.Metadata.ChangeVersion;
            var snapshot = await this.projectManager.ReadSceneForReloadAsync(context.Scene, cancellationToken).ConfigureAwait(true);
            cancellationToken.ThrowIfCancellationRequested();
            if (snapshot is null)
            {
                return this.ReloadFailure(context, "The file could not be read as this scene. Your unsaved changes are still open.");
            }

            if (context.Metadata.ChangeVersion != version || !this.CanReloadScene(context))
            {
                return this.ReloadFailure(context, "The document changed while reading its file. Confirm Reload again to discard the newer changes.");
            }

            accepted = await this.AcceptSceneReloadAsync(context, snapshot, replacement).ConfigureAwait(true);
            _ = await this.documentService.UpdateMetadataAsync(this.windowId, context.DocumentId, context.Metadata).ConfigureAwait(true);
            _ = this.messenger.Send(new AssetsChangedMessage());
            return SceneCommandResults.Success(accepted);
        }
        catch (OperationCanceledException) when (accepted is null)
        {
            return SceneCommandResults.Failure<Scene>() with { FailureMessage = "Reload cancelled. Your changes are still open." };
        }
        catch (Exception exception)
        {
            if (accepted is null)
            {
                return this.ReloadFailure(context, exception.Message);
            }

            var operation = this.PublishSceneWarning("Scene.Reload", DiagnosticCodes.DocumentPrefix + "RELOAD_REFRESH_FAILED", "Scene reloaded", $"The source was reloaded, but a document notification failed: {exception.Message}", context, domain: FailureDomain.Document);
            return new(Succeeded: true, accepted, operation);
        }
    }

    private bool CanReloadScene(SceneDocumentCommandContext context)
        => !context.History.IsBusy && context.Scene.Project.Scenes.Contains(context.Scene)
            && ReferenceEquals(this.sceneEngineSync.GetDocumentScene(context.Metadata), context.Scene);

    private async Task<Scene> AcceptSceneReloadAsync(SceneDocumentCommandContext context, SceneReloadSnapshot snapshot, SceneAuthoringGate.Replacement replacement)
    {
        if (!this.sceneEngineSync.RegisterDocument(snapshot.Scene, context.Metadata))
        {
            throw new InvalidOperationException("The document closed before the scene could be reloaded.");
        }

        Scene scene;
        try
        {
            scene = this.projectManager.AcceptSceneReload(snapshot);
        }
        catch
        {
            if (this.sceneEngineSync.RegisterDocument(context.Scene, context.Metadata)
                && this.documentService.GetActiveDocumentId(this.windowId) == context.DocumentId)
            {
                _ = await this.sceneEngineSync.SyncSceneWhenReadyAsync(context.Scene, CancellationToken.None).ConfigureAwait(true);
            }

            throw;
        }

        context.History.Clear();
        context.Metadata.IsDirty = false;
        replacement.Retire();
        return scene;
    }

    private SceneValueCommandResult<Scene> ReloadFailure(SceneDocumentCommandContext context, string message)
    {
        var operation = this.PublishSceneFailure("Scene.Reload", DiagnosticCodes.DocumentPrefix + "RELOAD_FAILED", "Scene was not reloaded", message, context, domain: FailureDomain.Document);
        return SceneCommandResults.Failure<Scene>(operation) with { FailureMessage = message };
    }
}
