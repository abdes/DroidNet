// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Projects;

namespace Oxygen.Editor.ContentPipeline.Import;

/// <summary>Reads what a model source holds before it is imported, through the ImportTool's <c>analyze-sources</c>,
/// which parses a file's structure without loading its buffers or building its geometry.</summary>
public interface IModelSourceAnalyzer
{
    /// <summary>Analyzes one model source.</summary>
    /// <param name="project">The project whose build folder holds the analysis's scratch files.</param>
    /// <param name="sourcePath">The model's primary source file.</param>
    /// <param name="cancellationToken">Cancels the analysis and its native worker.</param>
    /// <returns>The source's facts and the diagnostics its import will report.</returns>
    public Task<ModelSourceAnalysis> AnalyzeAsync(ProjectContext project, string sourcePath, CancellationToken cancellationToken);
}
