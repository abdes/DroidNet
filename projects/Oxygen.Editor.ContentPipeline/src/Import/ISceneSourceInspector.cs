// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Managed.Core.Compatibility;

namespace Oxygen.Editor.ContentPipeline.Import;

/// <summary>Discovers native source facts for coherent retention and import.</summary>
public interface ISceneSourceInspector
{
    /// <summary>Reads source metadata without cooking or publishing project content.</summary>
    /// <param name="operationId">The owning operation's correlation identity.</param>
    /// <param name="operationRoot">The owner of temporary query output.</param>
    /// <param name="sourcePath">The primary source file or its private captured copy.</param>
    /// <param name="cancellationToken">Cancels the query and drains its worker.</param>
    /// <param name="artifacts">Optional native artifacts retained by the enclosing operation.</param>
    /// <returns>Source facts or an actionable unsupported/invalid-source report.</returns>
    public Task<SceneSourceInspectionReport> InspectSceneSourceAsync(Guid operationId, string operationRoot, string sourcePath, CancellationToken cancellationToken, NativeArtifactLease? artifacts = null);
}
