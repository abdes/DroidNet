// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Projects;

/// <summary>
///     Validates project folders before they are activated into the workspace.
/// </summary>
public interface IProjectValidationService
{
    /// <summary>
    ///     Validates a project folder.
    /// </summary>
    /// <param name="projectFolderPath">The project folder path.</param>
    /// <param name="cancellationToken">A cancellation token.</param>
    /// <returns>The validation result.</returns>
    public Task<ProjectValidationResult> ValidateAsync(
        string projectFolderPath,
        CancellationToken cancellationToken = default);
}
