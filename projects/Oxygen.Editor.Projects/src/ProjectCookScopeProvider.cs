// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Projects;

/// <summary>
///     Default project cook scope provider.
/// </summary>
public sealed class ProjectCookScopeProvider : IProjectCookScopeProvider
{
    /// <inheritdoc/>
    public ProjectCookScope CreateScope(ProjectContext context)
    {
        ArgumentNullException.ThrowIfNull(context);

        return new ProjectCookScope(context.ProjectId, context.ProjectRoot);
    }
}
