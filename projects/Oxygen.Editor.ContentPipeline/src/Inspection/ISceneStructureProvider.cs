// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Projects;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.ContentPipeline.Inspection;

/// <summary>Reads what an instance of a source contains from the source's current cooked scene.</summary>
public interface ISceneStructureProvider
{
    /// <summary>Gets the source's composed structure without cooking or changing authoring data.</summary>
    /// <param name="project">The originating project.</param>
    /// <param name="source">A model's source file or an authored scene.</param>
    /// <param name="cancellationToken">Cancels inspection and drains any native reader.</param>
    /// <returns>The structure, or null when the source is not instanceable, not cooked, or its cook is stale.</returns>
    public Task<SceneStructure?> ReadAsync(ProjectContext project, AssetPath source, CancellationToken cancellationToken = default);
}
