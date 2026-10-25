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
using Oxygen.Editor.Projects.Testing;
using Oxygen.Editor.Cooking;
using Oxygen.Managed.Core;
using Oxygen.Managed.Core.Diagnostics;
using Testably.Abstractions.Testing;

namespace Oxygen.Editor.Cooking.Unit.UI.Tests;

[TestClass]
internal sealed class CookRetryViewModelTests : DroidNet.Tests.VisualUserInterfaceTests
{
    /// <summary>Retry dispatches initial imports and retained sources to their respective shared entry points.</summary>
    /// <param name="retained">Whether the earlier operation already saved its import record.</param>
    /// <returns>The asynchronous Cooking recovery command test.</returns>
    [TestMethod]
    [DataRow(true)]
    [DataRow(false)]
    public Task CookingRetryPreservesImportScope(bool retained) => EnqueueAsync(async () =>
    {
        var project = TestProject.Create(new MockFileSystem(), Path.Combine(Path.GetTempPath(), Guid.NewGuid().ToString("N"))).Context with { Name = "Import" };
        using var session = new TestSession(project);
        var source = AssetPath.Parse("/Content/Models/Crate/Crate.gltf");
        var request = new SceneImportRequest(project, Path.Combine(Path.GetTempPath(), "Crate.gltf"), "Crate", AssetPath.Parse("/Content/Models"));
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
        using var model = new CookingPanelViewModel(runs.Object, pipeline.Object, session, Mock.Of<ICookRecovery>(), hosting);
        _ = model.SelectedRun!.Kind.Should().Be("Source");
        await model.RetryCommand.ExecuteAsync(parameter: null).ConfigureAwait(true);
        _ = model.ActionError.Should().BeEmpty();
        pipeline.Verify(value => value.ImportSourceAsync(request, It.IsAny<CancellationToken>()), retained ? Times.Never() : Times.Once());
        pipeline.Verify(value => value.ReimportSourceAsync(source, project, It.IsAny<CancellationToken>()), retained ? Times.Once() : Times.Never());
    });

    /// <summary>Retry of a cook on change run of several sources cooks the same sources as one requested run.</summary>
    /// <returns>The asynchronous Cooking recovery command test.</returns>
    [TestMethod]
    public Task CookingRetryCooksTheSameChangedSources() => EnqueueAsync(async () =>
    {
        AssetPath[] assets = [AssetPath.Parse("/Content/Materials/Red.omat"), AssetPath.Parse("/Content/Input/Player.input")];
        var actions = new Mock<ICookRecovery>();
        _ = actions.Setup(value => value.SaveInputsAsync(It.IsAny<CookRunRequest>())).ReturnsAsync(value: true);
        var pipeline = new Mock<IContentPipelineService>();
        _ = pipeline.Setup(value => value.CookAssetsAsync(It.IsAny<IReadOnlyList<AssetPath>>(), It.IsAny<CancellationToken>()))
            .ReturnsAsync(new ContentCookResult(Guid.NewGuid(), CookTargetKind.Changes, OperationStatus.Succeeded, [], [], Inspection: null, Validation: null));
        using var session = CreateSession();
        using var model = CreateModel(session, "2 changed assets", new(CookTargetKind.Changes, ScopeUri: null, IsAutomatic: true) { Assets = assets }, actions.Object, pipeline.Object);
        _ = model.SelectedRun!.Context.Should().Be("Changes · Automatic");
        await model.RetryCommand.ExecuteAsync(parameter: null).ConfigureAwait(true);
        _ = model.ActionError.Should().BeEmpty();
        pipeline.Verify(value => value.CookAssetsAsync(It.Is<IReadOnlyList<AssetPath>>(cooked => cooked.SequenceEqual(assets)), It.IsAny<CancellationToken>()), Times.Once());
    });

    /// <summary>Retry cooks nothing when the cook's unsaved inputs are not saved.</summary>
    /// <returns>The asynchronous Cooking recovery command test.</returns>
    [TestMethod]
    public Task CookingRetryDoesNotCookWhenInputsAreNotSaved() => EnqueueAsync(async () =>
    {
        var actions = new Mock<ICookRecovery>();
        _ = actions.Setup(value => value.SaveInputsAsync(It.IsAny<CookRunRequest>())).ReturnsAsync(value: false);
        var pipeline = new Mock<IContentPipelineService>(MockBehavior.Strict);
        using var session = CreateSession();
        using var model = CreateModel(session, "Scenes", new(CookTargetKind.Project, ScopeUri: null), actions.Object, pipeline.Object);
        await model.RetryCommand.ExecuteAsync(parameter: null).ConfigureAwait(true);
        _ = model.ActionError.Should().BeEmpty();
        actions.Verify(value => value.SaveInputsAsync(It.IsAny<CookRunRequest>()), Times.Once());
        pipeline.VerifyNoOtherCalls();
    });

    /// <summary>Retry of a current scene run cooks the current scene of the same scope.</summary>
    /// <returns>The asynchronous Cooking recovery command test.</returns>
    [TestMethod]
    public Task CookingRetryOfCurrentSceneCooksTheCurrentScene() => EnqueueAsync(async () =>
    {
        var scope = AssetPath.Parse("/Content/Scenes/Main.oscene");
        var actions = new Mock<ICookRecovery>();
        _ = actions.Setup(value => value.SaveInputsAsync(It.IsAny<CookRunRequest>())).ReturnsAsync(value: true);
        var pipeline = new Mock<IContentPipelineService>();
        _ = pipeline.Setup(value => value.CookCurrentSceneAsync(scope, It.IsAny<CancellationToken>()))
            .ReturnsAsync(new ContentCookResult(Guid.NewGuid(), CookTargetKind.CurrentScene, OperationStatus.Succeeded, [], [], Inspection: null, Validation: null));
        using var session = CreateSession();
        using var model = CreateModel(session, "Main", new(CookTargetKind.CurrentScene, scope), actions.Object, pipeline.Object);
        await model.RetryCommand.ExecuteAsync(parameter: null).ConfigureAwait(true);
        _ = model.ActionError.Should().BeEmpty();
        pipeline.Verify(value => value.CookCurrentSceneAsync(scope, It.IsAny<CancellationToken>()), Times.Once());
    });

    private static TestSession CreateSession()
        => new(TestProject.Create(new MockFileSystem(), Path.Combine(Path.GetTempPath(), Guid.NewGuid().ToString("N"))).Context with { Name = "Retry" });

    private static CookingPanelViewModel CreateModel(TestSession session, string displayName, CookRunRequest request, ICookRecovery actions, IContentPipelineService pipeline)
    {
        var project = session.Context;
        var snapshot = new CookRunSnapshot
        {
            OperationId = Guid.NewGuid(),
            ProjectId = project.ProjectId,
            ProjectRoot = project.ProjectRoot,
            DisplayName = displayName,
            State = CookRunState.Failed,
            CompletedAt = DateTimeOffset.UtcNow,
            Request = request,
        };
        var runs = new Mock<ICookRunService>();
        _ = runs.SetupGet(value => value.Runs).Returns([snapshot]);
        var dispatcher = DispatcherQueue.GetForCurrentThread();
        var hosting = new HostingContext { Application = Application.Current, Dispatcher = dispatcher, DispatcherScheduler = new System.Reactive.Concurrency.DispatcherQueueScheduler(dispatcher) };
        return new(runs.Object, pipeline, session, actions, hosting);
    }
}
