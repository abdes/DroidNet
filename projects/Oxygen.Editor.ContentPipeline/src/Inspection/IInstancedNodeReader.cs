// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Projects;
using Oxygen.Editor.World;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.ContentPipeline.Inspection;

/// <summary>
/// Reads what an instance brings in, from the cooked scene descriptors of the mounted content, without loading what
/// they reference: the effective values of one of its nodes (scene instances 8.5), and its source's structure on
/// demand (8.2).
/// </summary>
public interface IInstancedNodeReader
{
    /// <summary>Gets the effective values of a node an instance of the source brings in.</summary>
    /// <param name="project">The originating project.</param>
    /// <param name="source">A model's source file or an authored scene.</param>
    /// <param name="path">The node's path below an instance root of the source.</param>
    /// <param name="cancellationToken">Cancels the read.</param>
    /// <returns>
    /// The values, or null when the source is not instanceable or not in the mounted content, or holds no such node.
    /// </returns>
    public Task<InstancedNodeValues?> ReadNodeAsync(ProjectContext project, AssetPath source, NodePath path, CancellationToken cancellationToken = default);

    /// <summary>
    /// Reads what an instance of a source brings in, as before swapping an instance to it, and keeps the structure with
    /// those <see cref="ISceneStructures"/> holds.
    /// </summary>
    /// <param name="project">The originating project.</param>
    /// <param name="source">A model's source file or an authored scene.</param>
    /// <param name="cancellationToken">Cancels the read.</param>
    /// <returns>
    /// The structure, or the last one read when the source cannot be read now; null when none was ever read: the
    /// source is not instanceable or not in the mounted content.
    /// </returns>
    public Task<SceneStructure?> ReadStructureAsync(ProjectContext project, AssetPath source, CancellationToken cancellationToken = default);
}
