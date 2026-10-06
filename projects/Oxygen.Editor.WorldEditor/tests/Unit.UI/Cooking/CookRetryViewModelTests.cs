// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using DroidNet.Hosting.WinUI;
using Microsoft.UI.Dispatching;
using Microsoft.UI.Xaml;
using Moq;
using Oxygen.Editor.ContentPipeline;
using Oxygen.Editor.ContentPipeline.Cooking;
using Oxygen.Editor.ContentPipeline.Import;
using Oxygen.Editor.Projects;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Cooking;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.WorldEditor.Unit.UI.Tests.Cooking;

[TestClass]
internal sealed class CookRetryViewModelTests : DroidNet.Tests.VisualUserInterfaceTests
{
    /// <summary>Retry dispatches initial imports and retained sources to their respective shared entry points.</summary>
    /// <param name="retained">Whether the earlier operation already saved its source settings.</param>
    /// <returns>The asynchronous Cooking recovery command test.</returns>
    [TestMethod]
    [DataRow(true)]
    [DataRow(false)]
    public Task CookingRetryPreservesImportScope(bool retained) => EnqueueAsync(async () =>
    {
        var projects = new ProjectContextService();
        var project = new ProjectContext
        {
            ProjectId = Guid.NewGuid(),
            Name = "Import",
            Category = Category.Games,
            ProjectRoot = Path.Combine(Path.GetTempPath(), Guid.NewGuid().ToString("N")),
            AuthoringMounts = [new("Content", "Content")],
            LocalFolderMounts = [],
            Scenes = [],
        };
        projects.Activate(project);
        var source = new Uri("asset:///Content/SourceMedia/DCC/Crate/Crate.gltf");
        var request = new SceneImportRequest(project, Path.Combine(Path.GetTempPath(), "Crate.gltf"), "Crate", new("asset:///Content/Models"));
        var snapshot = new CookRunSnapshot
        {
            OperationId = Guid.NewGuid(),
            ProjectId = project.ProjectId,
            ProjectRoot = project.ProjectRoot,
            DisplayName = "Crate",
            State = CookRunState.Failed,
            CompletedAt = DateTimeOffset.UtcNow,
            Request = new(CookTargetKind.Asset, source) { Import = retained ? null : request, IsReimport = retained },
        };
        var runs = new Mock<ICookRunService>();
        _ = runs.SetupGet(value => value.Runs).Returns([snapshot]);
        var pipeline = new Mock<IContentPipelineService>();
        var result = new ContentCookResult(Guid.NewGuid(), CookTargetKind.Asset, OperationStatus.Succeeded, [], [], Inspection: null, Validation: null);
        _ = pipeline.Setup(value => value.ImportSourceAsync(request, It.IsAny<CancellationToken>())).ReturnsAsync(result);
        _ = pipeline.Setup(value => value.ReimportSourceAsync(source, project, It.IsAny<CancellationToken>())).ReturnsAsync(result);
        var dispatcher = DispatcherQueue.GetForCurrentThread();
        var hosting = new HostingContext { Application = Application.Current, Dispatcher = dispatcher, DispatcherScheduler = new System.Reactive.Concurrency.DispatcherQueueScheduler(dispatcher) };
        using var model = new CookingPanelViewModel(runs.Object, pipeline.Object, projects, Mock.Of<ICookingWorkspaceActions>(), hosting);
        _ = model.SelectedRun!.Kind.Should().Be("Source");
        await model.RetryCommand.ExecuteAsync(parameter: null).ConfigureAwait(true);
        _ = model.ActionError.Should().BeEmpty();
        pipeline.Verify(value => value.ImportSourceAsync(request, It.IsAny<CancellationToken>()), retained ? Times.Never() : Times.Once());
        pipeline.Verify(value => value.ReimportSourceAsync(source, project, It.IsAny<CancellationToken>()), retained ? Times.Once() : Times.Never());
    });
}
