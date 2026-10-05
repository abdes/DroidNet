// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.World;
using Oxygen.Editor.World.Serialization;

namespace Oxygen.Editor.Projects;

public partial class ProjectManagerService
{
    /// <inheritdoc/>
    public async Task<SceneLoadSnapshot?> StageSceneLoadAsync(Scene scene, CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(scene);
        cancellationToken.ThrowIfCancellationRequested();
        var read = await this.ReadSceneFromStorageAsync(scene.Name, scene.Project, scene.Id, cancellationToken).ConfigureAwait(true);
        if (read is null)
        {
            return null;
        }

        var nodeIds = new HashSet<Guid>();
        if (read.Scene.AllNodes.Any(node => node.Id == Guid.Empty || !nodeIds.Add(node.Id)))
        {
            this.CouldNotLoadScene(read.SourcePath, "The source contains missing or duplicate authored node identities.");
            return null;
        }

        var folderIds = new HashSet<Guid>();
        var layoutNodeIds = new HashSet<Guid>();
        if (!ValidateLayout(read.Scene.ExplorerLayout))
        {
            this.CouldNotLoadScene(read.SourcePath, "The source contains duplicate or foreign explorer layout identities.");
            return null;
        }

        return new(this, scene, read.Scene.Dehydrate(), read.SourcePath, read.Version);

        bool ValidateLayout(IEnumerable<ExplorerEntryData>? entries)
        {
            if (entries is null)
            {
                return true;
            }

            foreach (var entry in entries)
            {
                if ((entry.FolderId is { } folder && !folderIds.Add(folder))
                    || (entry.NodeId is { } node && (!nodeIds.Contains(node) || !layoutNodeIds.Add(node)))
                    || !ValidateLayout(entry.Children))
                {
                    return false;
                }
            }

            return true;
        }
    }

    /// <inheritdoc/>
    public Scene AcceptSceneLoad(SceneLoadSnapshot snapshot)
    {
        ArgumentNullException.ThrowIfNull(snapshot);
        if (!ReferenceEquals(snapshot.Owner, this) || snapshot.Accepted
            || !snapshot.Original.Project.Scenes.Contains(snapshot.Original))
        {
            throw new InvalidOperationException("The staged source no longer belongs to this scene lifetime.");
        }

        var scene = Scene.CreateAndHydrate(snapshot.Original.Project, snapshot.Data);
        scene.SetExplorerLayout(snapshot.Data.ExplorerLayout);
        snapshot.Accepted = true;
        this.sceneVersions[snapshot.SourcePath] = snapshot.Version;
        this.sceneSources[SceneSourceKey(scene.Project.ProjectInfo.Location!, scene.Id)] = new(snapshot.SourcePath, snapshot.Version);
        return ReplaceLoadedScene(snapshot.Original, scene)!;
    }

    /// <inheritdoc/>
    public void RetireScene(Scene scene)
    {
        ArgumentNullException.ThrowIfNull(scene);
        var project = scene.Project;
        var index = project.Scenes.IndexOf(scene);
        if (index >= 0)
        {
            project.Scenes[index] = new Scene(project) { Id = scene.Id, Name = scene.Name };
        }

        if (ReferenceEquals(project.ActiveScene, scene))
        {
            project.ActiveScene = null;
        }
    }
}
