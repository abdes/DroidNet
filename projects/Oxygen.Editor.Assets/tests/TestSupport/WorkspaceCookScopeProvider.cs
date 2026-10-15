// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Projects;

namespace Oxygen.Editor.Assets.TestSupport;

/// <summary>Cooks a workspace's project into its <c>.cooked</c> folder.</summary>
/// <param name="workspace">The workspace whose output folder is used.</param>
internal sealed class WorkspaceCookScopeProvider(AssetWorkspace workspace) : IProjectCookScopeProvider
{
    /// <inheritdoc />
    public ProjectCookScope CreateScope(ProjectContext context)
        => new(context.ProjectId, context.ProjectRoot, workspace.SourcePath(".cooked"));
}
