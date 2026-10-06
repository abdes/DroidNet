// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json;
using CommunityToolkit.Mvvm.Messaging;
using DroidNet.Storage;
using DroidNet.TimeMachine;
using Oxygen.Editor.ContentBrowser.Messages;
using Oxygen.Editor.Projects;
using Oxygen.Editor.World.Documents.Commands;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.WorldEditor.Documents.Commands;

/// <summary>Coordinates persisted scene-asset renames with the document lifetime and history.</summary>
public sealed partial class SceneDocumentCommandService
{
    /// <inheritdoc/>
    public Task<SceneCommandResult> RenameSceneAsync(SceneDocumentCommandContext context, string newName)
        => this.RenameSceneCoreAsync(context, newName, replayingHistory: false);

    private static void ApplySceneReferenceRename(SceneDocumentCommandContext context, SceneAssetRenameResult result)
    {
        foreach (var scene in context.Scene.Project.Scenes)
        {
            if (!scene.References.ExtraAssets.Contains(result.PreviousAssetPath, StringComparer.Ordinal))
            {
                continue;
            }

            scene.SetReferences(scene.References with
            {
                ExtraAssets = scene.References.ExtraAssets.Select(path =>
                    string.Equals(path, result.PreviousAssetPath, StringComparison.Ordinal) ? result.AssetPath : path).ToArray(),
            });
        }
    }

    private async Task<SceneCommandResult> RenameSceneCoreAsync(
        SceneDocumentCommandContext context,
        string newName,
        bool replayingHistory)
    {
        using var authoring = EnterAuthoring(context);
        if (authoring is null)
        {
            return this.ValidationFailure(
                SceneOperationKinds.Rename,
                DiagnosticCodes.DocumentPrefix + "STALE_TARGET",
                "Scene was not renamed",
                "The scene document is no longer editable.",
                context);
        }

        await this.CompleteEditSessionsAsync(context, commit: true).ConfigureAwait(true);
        var gate = SaveGates.GetValue(context.Scene, static _ => new SemaphoreSlim(1, 1));
        await gate.WaitAsync().ConfigureAwait(true);
        SceneAssetRenameResult? committed = null;
        try
        {
            var version = context.Metadata.ChangeVersion;
            var wasDirty = context.Metadata.IsDirty;
            var result = await this.projectManager.RenameSceneAssetAsync(context.Scene, newName).ConfigureAwait(true);
            if (!result.Changed)
            {
                return SceneCommandResult.Success;
            }

            committed = result;
            await this.CompleteSceneRenameAsync(context, result, version, wasDirty).ConfigureAwait(true);
            return new(Succeeded: true) { HasUnsavedChanges = context.Metadata.IsDirty };
        }
        catch (Exception exception) when (exception is ArgumentException or InvalidOperationException or StorageException or IOException or InvalidDataException or JsonException or UnauthorizedAccessException)
        {
            if (committed is not null)
            {
                var warning = this.PublishSceneWarning(SceneOperationKinds.Rename, DiagnosticCodes.DocumentPrefix + "RENAME_REFRESH_FAILED", "Scene renamed; editor refresh incomplete", exception.Message, context, exception, FailureDomain.Document);
                return new(Succeeded: true, warning) { HasUnsavedChanges = context.Metadata.IsDirty };
            }

            var operation = this.PublishSceneFailure(SceneOperationKinds.Rename, DiagnosticCodes.DocumentPrefix + "RENAME_FAILED", "Scene was not renamed", exception.Message, context, exception, FailureDomain.Document);
            if (replayingHistory)
            {
                throw;
            }

            return new(Succeeded: false, operation)
            {
                ValidationMessage = exception.Message,
                IsConflict = exception is StorageWriteConflictException,
                HasUnsavedChanges = context.Metadata.IsDirty,
            };
        }
        finally
        {
            _ = gate.Release();
        }
    }

    private async Task CompleteSceneRenameAsync(SceneDocumentCommandContext context, SceneAssetRenameResult result, long version, bool wasDirty)
    {
        context.History.AddChange(
            $"Rename scene ({result.PreviousName} -> {result.Name})",
            () => this.ReplaySceneRenameAsync(context, result.PreviousName));
        ApplySceneReferenceRename(context, result);
        context.Metadata.Title = result.Name;
        var newerChanges = context.Metadata.ChangeVersion != version;
        context.Metadata.IsDirty = true;
        _ = this.sceneEngineSync.CaptureRevision(context.Scene, context.Metadata);
        if (!wasDirty && !newerChanges)
        {
            context.Metadata.MarkSaved(context.Metadata.ChangeVersion);
        }

        try
        {
            if (!await this.documentService.UpdateMetadataAsync(this.windowId, context.DocumentId, context.Metadata).ConfigureAwait(true))
            {
                throw new InvalidOperationException("The scene asset was renamed, but its document tab could not be refreshed.");
            }
        }
        finally
        {
            foreach (var source in result.Sources)
            {
                if (!string.Equals(source.Previous.SourcePath, source.Current.SourcePath, StringComparison.Ordinal))
                {
                    automaticCooking.NotifyRenamed(source.Previous.SourcePath, source.Current.SourcePath, source.Current.Version.Sha256);
                }
                else
                {
                    automaticCooking.NotifySaved(source.Current.SourcePath, source.Current.Version.Sha256);
                }
            }

            _ = this.messenger.Send(new AssetsChangedMessage());
        }
    }

    private async Task ReplaySceneRenameAsync(SceneDocumentCommandContext context, string name)
    {
        var result = await this.RenameSceneCoreAsync(context, name, replayingHistory: true).ConfigureAwait(true);
        if (!result.Succeeded)
        {
            throw new InvalidOperationException(result.ValidationMessage ?? "The scene rename could not be replayed.");
        }
    }
}
