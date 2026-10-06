// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.Extensions.Logging.Abstractions;
using Oxygen.Editor.Projects;
using Oxygen.Editor.World;

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

    internal static ProjectContext CreateContext() => new()
    {
        ProjectId = Guid.NewGuid(),
        Name = "Cook coordination",
        Category = Category.Games,
        ProjectRoot = Path.GetTempPath(),
        AuthoringMounts = [new ProjectMountPoint("Content", "Content")],
        LocalFolderMounts = [],
        Scenes = [],
    };
}
