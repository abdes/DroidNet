// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json;
using CommunityToolkit.Mvvm.Messaging;
using Oxygen.Editor.ContentBrowser.Messages;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Serialization;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.WorldEditor.Documents.Commands;

/// <summary>Creates independent authoring assets while preserving the source document's dirty state.</summary>
public sealed partial class SceneDocumentCommandService
{
    /// <inheritdoc/>
    public async Task<SceneValueCommandResult<Scene>> SaveSceneCopyAsync(SceneDocumentCommandContext context, string name)
    {
        using var authoring = EnterAuthoring(context);
        if (authoring is null)
        {
            return SceneCommandResults.Failure<Scene>();
        }

        ArgumentNullException.ThrowIfNull(context);
        await this.CompleteEditSessionsAsync(context, commit: true).ConfigureAwait(true);
        var gate = SaveGates.GetValue(context.Scene, static _ => new SemaphoreSlim(1, 1));
        await gate.WaitAsync().ConfigureAwait(true);
        try
        {
            await this.CompleteEditSessionsAsync(context, commit: true).ConfigureAwait(true);
            ArgumentException.ThrowIfNullOrWhiteSpace(name);
            if (name.IndexOfAny(Path.GetInvalidFileNameChars()) >= 0 || name is "." or ".."
                || string.Equals(name, context.Scene.Name, StringComparison.OrdinalIgnoreCase))
            {
                throw new ArgumentException("Choose a different scene name without path separators.", nameof(name));
            }

            var original = SceneSaveSnapshot.Capture(context.Scene);
            var data = JsonSerializer.Deserialize(original.Json, SceneJsonContext.Default.SceneData)!
                with { Id = Guid.NewGuid(), Name = name, };
            var snapshot = new SceneSaveSnapshot(data.Id, name, original.ProjectLocation, JsonSerializer.Serialize(data, SceneJsonContext.Default.SceneData));
            if (!await this.projectManager.CreateSceneSnapshotAsync(snapshot).ConfigureAwait(true))
            {
                throw new IOException("The new scene could not be saved.");
            }

            var copy = Scene.CreateAndHydrate(context.Scene.Project, data);
            context.Scene.Project.Scenes.Add(copy);
            _ = this.messenger.Send(new AssetsChangedMessage());
            return SceneCommandResults.Success(copy);
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException or ArgumentException)
        {
            var operation = this.PublishSceneFailure(SceneOperationKinds.Save, DiagnosticCodes.DocumentPrefix + "COPY_FAILED", "Scene copy was not saved", exception.Message, context, exception, FailureDomain.Document);
            return SceneCommandResults.Failure<Scene>(operation);
        }
        finally
        {
            _ = gate.Release();
        }
    }
}
