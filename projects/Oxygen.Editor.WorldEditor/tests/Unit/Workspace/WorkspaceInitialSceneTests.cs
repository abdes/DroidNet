// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Editor.Projects;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Workspace;

namespace Oxygen.Editor.WorldEditor.Unit.Tests.Workspace;

[TestClass]
public sealed class WorkspaceInitialSceneTests
{
    [TestMethod]
    public void ResolveSceneByNameOrId_MatchesByNameFileStemOrId()
    {
        var project = CreateProjectWithScenes(out var byName, out var byStem, out var byId);

        _ = WorkspaceViewModel.ResolveSceneByNameOrId(project, byName.Name).Should().BeSameAs(byName);
        _ = WorkspaceViewModel.ResolveSceneByNameOrId(project, $"{byStem.Name}.oscene.json").Should().BeSameAs(byStem);
        _ = WorkspaceViewModel.ResolveSceneByNameOrId(project, byId.Id.ToString("D")).Should().BeSameAs(byId);
    }

    [TestMethod]
    public void ResolveSceneByNameOrId_ReturnsNullForBlankOrUnknown()
    {
        var project = CreateProjectWithScenes(out _, out _, out _);

        _ = WorkspaceViewModel.ResolveSceneByNameOrId(project, "   ").Should().BeNull();
        _ = WorkspaceViewModel.ResolveSceneByNameOrId(project, "Not a scene").Should().BeNull();
    }

    private static Project CreateProjectWithScenes(out Scene byName, out Scene byStem, out Scene byId)
    {
        var project = new Project(new ProjectInfo("Migration", Category.Games, "H:/MigrationTests", "preview.png")) { Name = "Migration" };
        byName = new Scene(project) { Name = "Alpha" };
        byStem = new Scene(project) { Name = "Beta" };
        byId = new Scene(project) { Name = "Gamma" };
        project.Scenes.Add(byName);
        project.Scenes.Add(byStem);
        project.Scenes.Add(byId);
        return project;
    }
}
