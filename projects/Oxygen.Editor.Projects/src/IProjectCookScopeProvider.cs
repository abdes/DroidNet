// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Projects;

/// <summary>
///     Produces the project-level cook scope policy.
/// </summary>
public interface IProjectCookScopeProvider
{
    /// <summary>
    ///     Creates the minimal cook scope for an active project.
    /// </summary>
    /// <param name="context">The active project context.</param>
    /// <returns>The project cook scope.</returns>
    public ProjectCookScope CreateScope(ProjectContext context);
}
