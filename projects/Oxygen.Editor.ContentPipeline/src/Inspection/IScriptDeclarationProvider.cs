// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Projects;

namespace Oxygen.Editor.ContentPipeline.Inspection;

/// <summary>Reads the parameters a script from a mounted cooked library declares.</summary>
/// <remarks>Scripts with a source in the project are read from their source by script analysis instead.</remarks>
public interface IScriptDeclarationProvider
{
    /// <summary>Gets a library script's declarations without cooking or changing authoring data.</summary>
    /// <param name="project">The originating project.</param>
    /// <param name="scriptUri">The script identity.</param>
    /// <param name="cancellationToken">Cancels inspection and drains any native reader.</param>
    /// <returns>The declarations, or null when the script does not come from a mounted library.</returns>
    public Task<CookedScriptDeclarations?> ReadAsync(ProjectContext project, Uri scriptUri, CancellationToken cancellationToken = default);
}
