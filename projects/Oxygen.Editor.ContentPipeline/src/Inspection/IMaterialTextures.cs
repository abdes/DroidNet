// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using Oxygen.Editor.Projects;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.ContentPipeline.Inspection;

/// <summary>Reads which textures the materials an import produced use, from the cooked materials.</summary>
public interface IMaterialTextures
{
    /// <summary>Gets the textures of each material a source's last cook produced.</summary>
    /// <param name="project">The originating project.</param>
    /// <param name="source">The source, such as a model's source file.</param>
    /// <param name="cancellationToken">Cancels inspection and drains any native reader.</param>
    /// <returns>
    /// The logical names of the textures each material binds, such as <c>/Content/Car/Paint_Normal.otex</c>, by the
    /// material's asset key; empty when the source has no current published product.
    /// </returns>
    public Task<ImmutableDictionary<Guid, ImmutableArray<string>>> ReadTexturesAsync(ProjectContext project, AssetPath source, CancellationToken cancellationToken = default);
}
