// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Projects;

/// <summary>
///     Creates new Oxygen projects from template payloads.
/// </summary>
public interface IProjectCreationService
{
    /// <summary>
    ///     Determines whether a project folder can be created.
    /// </summary>
    /// <param name="projectName">The new project name.</param>
    /// <param name="parentLocation">The parent location.</param>
    /// <param name="cancellationToken">A cancellation token.</param>
    /// <returns><see langword="true"/> when the target folder can be used.</returns>
    public Task<bool> CanCreateProjectAsync(
        string projectName,
        string parentLocation,
        CancellationToken cancellationToken = default);

    /// <summary>
    ///     Creates a project from a template payload and validates the result.
    /// </summary>
    /// <param name="request">The project creation request.</param>
    /// <param name="cancellationToken">A cancellation token.</param>
    /// <returns>The project creation result.</returns>
    public Task<ProjectCreationResult> CreateFromTemplateAsync(
        ProjectCreationRequest request,
        CancellationToken cancellationToken = default);
}
