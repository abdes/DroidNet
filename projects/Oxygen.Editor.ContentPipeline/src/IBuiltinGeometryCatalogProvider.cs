// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Managed.Core.Compatibility;

namespace Oxygen.Editor.ContentPipeline;

/// <summary>Provides engine-owned procedural identities and descriptor contributions.</summary>
public interface IBuiltinGeometryCatalogProvider
{
    /// <summary>Queries definitions without cooking or publishing any project asset.</summary>
    /// <param name="projectRoot">The owner of temporary query output.</param>
    /// <param name="mountName">The virtual mount selected by project policy.</param>
    /// <param name="cancellationToken">Cancels the query and drains its native worker.</param>
    /// <param name="artifacts">Optional qualified artifacts retained by the enclosing cook operation.</param>
    /// <returns>The immutable native catalog for the requested output mount.</returns>
    public Task<BuiltinGeometryCatalog> GetBuiltinGeometryCatalogAsync(string projectRoot, string mountName, CancellationToken cancellationToken, QualifiedArtifactLease? artifacts = null);
}
