// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using AwesomeAssertions;
using Microsoft.Extensions.Logging.Abstractions;
using Oxygen.Editor.ContentPipeline.Publication;
using Oxygen.Editor.Projects;
using Oxygen.Editor.World.Serialization;
using Oxygen.Editor.World;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.ContentPipeline.TestSupport;

internal sealed class FixedCookScopeProvider(string projectRoot) : IProjectCookScopeProvider
{
    public ProjectCookScope CreateScope(ProjectContext context)
        => new(context.ProjectId, projectRoot, Path.Combine(projectRoot, ".cooked"));
}
