// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.ContentPipeline;
using Oxygen.Editor.ContentPipeline.Inspection;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Runtime.Engine;
using Oxygen.Editor.World;
using Oxygen.Managed.Assets;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.LivePreview.Scenes;

/// <summary>
/// What an instance of a source contains (scene instances 8.2), read by the engine that shows the active scene from
/// the cooked scene descriptors of the content it mounts: the source's nodes and those of the scenes it instances,
/// without loading geometry or anything else they reference.
/// </summary>
internal sealed partial class ScenePreview : ISceneStructureProvider
{
    /// <inheritdoc/>
    event EventHandler? ISceneStructureProvider.Changed
    {
        add => this.StructuresChanged += value;
        remove => this.StructuresChanged -= value;
    }

    // Raised on a document's owner thread when it becomes ready, and whenever the engine mounts the project's content
    // again, as after a cook: structures read before may differ.
    private event EventHandler? StructuresChanged;

    /// <inheritdoc/>
    async Task<SceneStructure?> ISceneStructureProvider.ReadAsync(ProjectContext project, AssetPath source, CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(project);
        ArgumentNullException.ThrowIfNull(source);
        if (!ReferenceEquals(this.session.Context, project)
            || AssetFileTypes.SceneProductOf(source) is not { } scene
            || this.activeWorld is not { } world)
        {
            return null;
        }

        var observation = await world.Commands.ReadSceneHierarchyAsync(Guid.NewGuid(), world.Target, ContentPipelinePaths.ToCookedScenePath(scene), cancellationToken).ConfigureAwait(false);
        return observation.Outcome.Succeeded && observation.Hierarchy is { Exists: true } hierarchy
            ? ToStructure(source, ContentPipelinePaths.ToCookedScenePath(scene), hierarchy)
            : null;
    }

    /// <summary>Converts the engine's hierarchy of a source's scene into the source's structure.</summary>
    /// <param name="source">The source.</param>
    /// <param name="virtualPath">The virtual path of the source's cooked scene.</param>
    /// <param name="hierarchy">The hierarchy the engine read.</param>
    /// <returns>The structure: each node by its path, its parent's path, the scene defining it and the one it instances.</returns>
    internal static SceneStructure ToStructure(AssetPath source, string virtualPath, RuntimeSceneHierarchy hierarchy)
    {
        var read = new StructureScene(hierarchy.SceneKey, virtualPath);
        var scenes = new Dictionary<Guid, StructureScene> { [read.Key] = read };
        StructureScene SceneOf(Guid key)
        {
            if (!scenes.TryGetValue(key, out var known))
            {
                known = new StructureScene(key, VirtualPath: null);
                scenes[key] = known;
            }

            return known;
        }

        var nodes = hierarchy.Nodes;
        return new SceneStructure(
            source,
            read,
            [
                .. nodes.Select(node => new StructureNode(
                    node.Path,
                    node.Name,
                    node.Parent is { } parent ? nodes[parent].Path : null,
                    SceneOf(node.SceneKey),
                    node.InstanceKey is { } instance ? SceneOf(instance) : null)),
            ],
            [
                .. hierarchy.Issues.Select(issue => new StructureIssue(
                    IssueCodeOf(issue.Code),
                    SceneOf(issue.SceneKey),
                    issue.Node is { } node ? nodes[node].Path : null,
                    issue.SceneKey)),
            ]);
    }

    private static string IssueCodeOf(RuntimeSceneHierarchyIssueCode code) => code switch
    {
        RuntimeSceneHierarchyIssueCode.SceneUnavailable => "scene.instance.unavailable",
        RuntimeSceneHierarchyIssueCode.Cycle => "scene.instance.cycle",
        RuntimeSceneHierarchyIssueCode.InvalidInstance => "scene.instance.invalid",
        _ => throw new ArgumentOutOfRangeException(nameof(code), code, "The engine reported an unknown hierarchy issue."),
    };

    // A scene shown before its content was mounted stays ready while the content arrives, so the mount itself announces
    // that structures may have changed.
    private void OnContentStatusChanged(object? sender, RuntimeContentChangedEventArgs args)
    {
        _ = sender;
        if (args.Snapshot.State == RuntimeContentState.Mounted)
        {
            this.owner.Queue(() => this.StructuresChanged?.Invoke(this, EventArgs.Empty));
        }
    }
}
