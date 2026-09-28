// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Projects;

namespace Oxygen.Editor.ContentPipeline.Inspection;

/// <summary>Reads native slot metadata using the current project and source precedence.</summary>
public interface IGeometryMaterialSlotProvider
{
    /// <summary>Gets the current inventory without cooking or changing authoring data.</summary>
    /// <param name="project">The originating project.</param>
    /// <param name="geometryUri">The authored geometry identity.</param>
    /// <param name="cancellationToken">Cancels inspection and drains any native reader.</param>
    /// <returns>The native inventory, or null when no verified current inventory is available.</returns>
    public Task<GeometryMaterialSlotMetadata?> ReadAsync(ProjectContext project, Uri geometryUri, CancellationToken cancellationToken = default);
}
