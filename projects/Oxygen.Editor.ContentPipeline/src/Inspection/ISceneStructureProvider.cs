// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Projects;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.ContentPipeline.Inspection;

/// <summary>
/// Reads what an instance of a source contains: its nodes and those of the scenes it instances, from the cooked scene
/// descriptors of the mounted content, without loading what they reference.
/// </summary>
public interface ISceneStructureProvider
{
    /// <summary>
    /// Occurs when a structure read before may differ now: content was mounted again, as after a cook or a reimport,
    /// or a scene became readable.
    /// </summary>
    public event EventHandler? Changed;

    /// <summary>Gets the source's structure without cooking or changing authoring data.</summary>
    /// <param name="project">The originating project.</param>
    /// <param name="source">A model's source file or an authored scene.</param>
    /// <param name="cancellationToken">Cancels the read.</param>
    /// <returns>The structure, or null when the source is not instanceable or not in the mounted content.</returns>
    public Task<SceneStructure?> ReadAsync(ProjectContext project, AssetPath source, CancellationToken cancellationToken = default);
}
