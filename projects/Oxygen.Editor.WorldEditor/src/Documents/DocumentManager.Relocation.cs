// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json;
using CommunityToolkit.Mvvm.Messaging;
using Microsoft.Extensions.Logging;
using Oxygen.Editor.ContentBrowser.Messages;
using Oxygen.Editor.ContentPipeline.Relocation;
using Oxygen.Editor.MaterialEditor;
using Oxygen.Editor.Projects;
using Oxygen.Editor.World.Serialization;

namespace Oxygen.Editor.World.Documents;

/// <summary>
/// Content Browser changes to open documents and scenes. Open documents follow renames and moves in memory as
/// relocation participants; here a deleted material's tab closes and moved materials' tabs refresh. Saved scenes
/// that are not open are renamed, duplicated or deleted here, because the project identifies scenes by name; the
/// open scene's editor renames it.
/// </summary>
public sealed partial class DocumentManager
{
    private void RegisterRelocationMessages()
    {
        this.messenger.Register<AssetFilesChangedMessage>(this, (_, message) => _ = this.FollowChangedFilesAsync(message));
        this.messenger.Register<SceneAssetCommandMessage>(this, (_, message) =>
        {
            if (message.HasReceivedResponse || this.projectManager.CurrentProject is not { } project)
            {
                return;
            }

            var scene = project.Scenes.FirstOrDefault(item => string.Equals(item.Name, message.SceneName, StringComparison.OrdinalIgnoreCase));
            if (scene is null)
            {
                this.LogSceneCommandRejected(message.Command, message.SceneName, "not part of this project");
                message.Reply(Task.FromResult<string?>($"Scene '{message.SceneName}' is not part of this project."));
                return;
            }

            var isOpen = this.documentService.GetOpenDocuments(this.windowId).Any(document => document.DocumentId == scene.Id);
            switch (message.Command)
            {
                case SceneAssetCommand.Rename when isOpen:
                    // The open scene's editor renames it, keeping its document and history in step.
                    return;
                case SceneAssetCommand.Rename:
                    message.Reply(this.LogSceneCommandAsync(message.Command, scene.Name, this.RenameSavedSceneAsync(scene, message.NewName)));
                    return;
                case SceneAssetCommand.Duplicate:
                    message.Reply(this.LogSceneCommandAsync(message.Command, scene.Name, this.DuplicateSavedSceneAsync(project, scene, isOpen)));
                    return;
                default:
                    message.Reply(this.LogSceneCommandAsync(message.Command, scene.Name, this.DeleteSceneAsync(project, scene, isOpen, message.Recycle)));
                    return;
            }
        });
    }

    private async Task<string?> RenameSavedSceneAsync(Scene scene, string? name)
    {
        if (string.IsNullOrWhiteSpace(name))
        {
            return "Enter a scene name.";
        }

        try
        {
            var previous = scene.Name;
            _ = await this.projectManager.RenameSceneAssetAsync(scene, name.Trim()).ConfigureAwait(true);
            this.LogSavedSceneRenamed(previous, scene.Name);
            return null;
        }
        catch (Exception error) when (error is IOException or UnauthorizedAccessException or ArgumentException or InvalidOperationException or DroidNet.Storage.StorageException)
        {
            return error.Message;
        }
    }

    // A clean open scene equals its saved file, so a copy of the file is a copy of the scene.
    private async Task<string?> DuplicateSavedSceneAsync(IProject project, Scene scene, bool isOpen)
    {
        if (isOpen && this.documentService.GetOpenDocuments(this.windowId).FirstOrDefault(document => document.DocumentId == scene.Id) is { IsDirty: true })
        {
            return $"Save scene '{scene.Name}' before duplicating it.";
        }

        if (project.ProjectInfo.Location is not { } location)
        {
            return "The project has no location.";
        }

        try
        {
            var source = this.projectManager.GetSceneSourceVersion(scene)?.SourcePath
                ?? Path.Combine(location, Oxygen.Editor.Projects.Constants.ContentFolderName, Oxygen.Editor.Projects.Constants.ScenesFolderName, scene.Name + Oxygen.Editor.Projects.Constants.SceneFileExtension);
            var json = await File.ReadAllTextAsync(source).ConfigureAwait(true);
            var name = scene.Name;
            for (var index = 2; project.Scenes.Any(item => string.Equals(item.Name, name, StringComparison.OrdinalIgnoreCase)); index++)
            {
                name = string.Create(System.Globalization.CultureInfo.InvariantCulture, $"{scene.Name} ({index})");
            }

            var data = JsonSerializer.Deserialize(json, SceneJsonContext.Default.SceneData)! with { Id = Guid.NewGuid(), Name = name };
            var snapshot = new SceneSaveSnapshot(data.Id, name, location, JsonSerializer.Serialize(data, SceneJsonContext.Default.SceneData));
            if (!await this.projectManager.CreateSceneSnapshotAsync(snapshot).ConfigureAwait(true))
            {
                return "The new scene could not be saved.";
            }

            project.Scenes.Add(Scene.CreateAndHydrate(project, data));
            this.LogSceneDuplicated(scene.Name, name);
            return null;
        }
        catch (Exception error) when (error is IOException or UnauthorizedAccessException or JsonException or DroidNet.Storage.StorageException)
        {
            return error.Message;
        }
    }

    private async Task<string?> DeleteSceneAsync(IProject project, Scene scene, bool isOpen, Func<IReadOnlyList<string>, Task>? recycle)
    {
        if (isOpen)
        {
            return $"Scene '{scene.Name}' is open. Open another scene before deleting it.";
        }

        if (project.ProjectInfo.DefaultSceneId == scene.Id)
        {
            return $"Scene '{scene.Name}' is the project's start scene. Choose another start scene before deleting it.";
        }

        if (recycle is null || project.ProjectInfo.Location is not { } location)
        {
            return "The scene cannot be deleted here.";
        }

        var source = this.projectManager.GetSceneSourceVersion(scene)?.SourcePath
            ?? Path.Combine(location, Oxygen.Editor.Projects.Constants.ContentFolderName, Oxygen.Editor.Projects.Constants.ScenesFolderName, scene.Name + Oxygen.Editor.Projects.Constants.SceneFileExtension);
        try
        {
            await recycle([source]).ConfigureAwait(true);
        }
        catch (IOException error)
        {
            return error.Message;
        }

        _ = project.Scenes.Remove(scene);
        this.LogSceneDeleted(scene.Name, source);
        return null;
    }

    // Open materials follow renames and moves in memory as relocation participants; here a deleted material's tab
    // closes, and a moved material's tab refreshes the title its editor already re-pointed.
    private async Task FollowChangedFilesAsync(AssetFilesChangedMessage message)
    {
        if (this.disposed || this.projectContextService.ActiveProject is not { } project)
        {
            return;
        }

        foreach (var metadata in this.documentService.GetOpenDocuments(this.windowId).OfType<MaterialDocumentMetadata>().ToArray())
        {
            // The tab's identity is its material URI; the material service keys documents by its own ids.
            if (RelocationPaths.ToPhysical(project, Uri.UnescapeDataString(metadata.MaterialUri.AbsolutePath)) is not { } path)
            {
                continue;
            }

            var deleted = message.DeletedFiles.Any(item => string.Equals(path, item, StringComparison.OrdinalIgnoreCase)
                || path.StartsWith(item.TrimEnd(Path.DirectorySeparatorChar) + Path.DirectorySeparatorChar, StringComparison.OrdinalIgnoreCase));
            if (deleted)
            {
                if (await this.documentService.CloseDocumentAsync(this.windowId, metadata.DocumentId, force: true).ConfigureAwait(true))
                {
                    this.LogDeletedMaterialClosed(metadata.MaterialUri);
                }
                else
                {
                    this.LogDeletedMaterialNotClosed(metadata.MaterialUri);
                }
            }
            else if (message.Moves.Any(move => string.Equals(path, move.Target, StringComparison.OrdinalIgnoreCase)
                || (move.IsDirectory && path.StartsWith(move.Target + Path.DirectorySeparatorChar, StringComparison.OrdinalIgnoreCase))))
            {
                _ = await this.documentService.UpdateMetadataAsync(this.windowId, metadata.DocumentId, metadata).ConfigureAwait(true);
            }
        }
    }

    private async Task<string?> LogSceneCommandAsync(SceneAssetCommand command, string scene, Task<string?> work)
    {
        var error = await work.ConfigureAwait(true);
        if (error is not null)
        {
            this.LogSceneCommandRejected(command, scene, error);
        }

        return error;
    }
}
