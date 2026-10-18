// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Projects;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.ContentPipeline.TestSupport;

internal sealed class FixedCookScopeProvider(string projectRoot) : IProjectCookScopeProvider
{
    public ProjectCookScope CreateScope(ProjectContext context)
        => new(context.ProjectId, FileSystemPath.From(projectRoot), Path.Combine(projectRoot, ".cooked"));
}
