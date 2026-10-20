// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.Extensions.Logging.Abstractions;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Projects.Testing;
using Testably.Abstractions;

namespace Oxygen.Editor.ContentPipeline.TestSupport;

internal static class CookCoordinatorScenario
{
    internal static ContentCookCoordinator CreateCoordinator(ProjectContextService context) => new(context, NullLogger<ContentCookCoordinator>.Instance);

    internal static ProjectContextService CreateContextService()
    {
        var service = new ProjectContextService();
        service.Activate(CreateContext());
        return service;
    }

    // The coordinator takes cross-process cook ownership with native file handles under the project root, so the
    // project is on the disk.
    internal static ProjectContext CreateContext()
        => TestProject.Create(new RealFileSystem(), Path.GetTempPath()).Context with { Name = "Cook coordination" };
}
