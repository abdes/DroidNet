// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.World;

namespace Oxygen.Editor.Projects;

public partial class ProjectManagerService
{
    /// <inheritdoc/>
    public Task<SceneAssetRenameResult> RenameSceneAssetAsync(Scene scene, string newName, CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(scene);
        var name = SceneAssetRenameOperation.ValidateName(newName);
        var projectRoot = scene.Project.ProjectInfo.Location
            ?? throw new InvalidOperationException("The scene has no project location.");
        return this.sceneWrites.RunAsync(
            projectRoot,
            async () =>
            {
                if (!scene.Project.Scenes.Contains(scene))
                {
                    throw new InvalidOperationException("The scene no longer belongs to the project.");
                }

                if (scene.Project.Scenes.Any(other => other.Id != scene.Id && string.Equals(other.Name, name, StringComparison.OrdinalIgnoreCase)))
                {
                    throw new ArgumentException("Another scene in the project already has that name.", nameof(newName));
                }

                var source = this.GetSceneSourceVersion(scene)
                    ?? throw new InvalidOperationException("Save the scene before renaming its asset.");
                var operation = new SceneAssetRenameOperation(storage, this.AtomicFiles);
                var result = await operation.ExecuteAsync(scene, name, source, this.sceneVersions, cancellationToken).ConfigureAwait(true);
                foreach (var change in result.Sources)
                {
                    _ = this.sceneVersions.TryRemove(change.Previous.SourcePath, out _);
                    this.sceneVersions[change.Current.SourcePath] = change.Current.Version;
                    this.sceneSources[SceneSourceKey(projectRoot, change.SceneId)] = change.Current;
                }

                scene.Name = result.Name;
                return result;
            },
            cancellationToken);
    }
}
