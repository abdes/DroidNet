// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Reactive.Linq;
using AwesomeAssertions;
using Moq;
using Oxygen.Editor.Assets;
using Oxygen.Editor.ContentPipeline;
using Oxygen.Editor.ContentPipeline.Inspection;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Projects.Testing;
using Oxygen.Editor.Shell.Windows;
using Oxygen.Editor.CookedInspector;
using Oxygen.Managed.Core;
using Testably.Abstractions.Testing;

namespace Oxygen.Editor.CookedInspector.Unit.Tests;

/// <summary>The Cooked Inspector is told what to inspect by a request, and comes forward.</summary>
[TestClass]
public sealed class CookedInspectionsTests
{
    /// <summary>A request sets the panel's scope to the project, the folder or asset, and the validation it asks for, then shows the panel.</summary>
    /// <returns>A task that completes when the test has run.</returns>
    [TestMethod]
    public async Task RequestInspectsItsScopeAndShowsThePanelAsync()
    {
        var project = TestProject.Create(new MockFileSystem()).Context with { ProjectId = Guid.NewGuid(), Name = "Test", AuthoringMounts = [] };
        var projects = new Mock<IProjectContextService>();
        _ = projects.SetupGet(static service => service.ActiveProject).Returns(project);
        var folder = AssetPath.Parse("/Content/Materials");
        var pipeline = new Mock<IContentPipelineService>();
        _ = pipeline.Setup(value => value.InspectCookedOutputAsync(folder, It.IsAny<CancellationToken>(), true, project))
            .ReturnsAsync(new CookedOutputReport(project.ProjectId, folder, DateTimeOffset.UtcNow, []));
        var placement = new Mock<IPlacementService>();
        using var model = new CookedInspectionViewModel(pipeline.Object, Mock.Of<IAssetFeed>(feed => feed.Items == Observable.Never<IReadOnlyList<AssetEntry>>()), projects.Object, _ => Task.FromResult(true));

        var taken = await new CookedInspections(model, projects.Object, placement.Object).InspectAsync(new CookedInspectionRequest(folder, Validate: true)).ConfigureAwait(true);
        await model.CurrentWork.ConfigureAwait(true);

        _ = taken.Should().BeTrue();
        pipeline.Verify(value => value.InspectCookedOutputAsync(folder, It.IsAny<CancellationToken>(), true, project), Times.Once);
        placement.Verify(service => service.ShowPanelAsync(CookedInspections.PanelId, It.IsAny<WindowPlacement>()), Times.Once);
    }

    /// <summary>Without an active project there is nothing to inspect and the panel stays where it is.</summary>
    /// <returns>A task that completes when the test has run.</returns>
    [TestMethod]
    public async Task RequestWithoutAProjectIsRefusedAsync()
    {
        var projects = new Mock<IProjectContextService>();
        var placement = new Mock<IPlacementService>();
        using var model = new CookedInspectionViewModel(Mock.Of<IContentPipelineService>(), Mock.Of<IAssetFeed>(), projects.Object, _ => Task.FromResult(true));

        var taken = await new CookedInspections(model, projects.Object, placement.Object).InspectAsync(new CookedInspectionRequest(Scope: null, Validate: false)).ConfigureAwait(true);

        _ = taken.Should().BeFalse();
        placement.VerifyNoOtherCalls();
    }
}
