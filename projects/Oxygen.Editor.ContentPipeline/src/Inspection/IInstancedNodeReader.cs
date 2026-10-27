// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Projects;
using Oxygen.Editor.World;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.ContentPipeline.Inspection;

/// <summary>
/// Reads the effective values of a node an instance brings in (scene instances 8.5), from the cooked scene
/// descriptors of the mounted content, without loading what they reference.
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
}
