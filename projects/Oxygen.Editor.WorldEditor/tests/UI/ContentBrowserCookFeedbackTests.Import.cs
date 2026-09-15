// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using CommunityToolkit.Mvvm.Messaging;
using DroidNet.Aura.Dialogs;
using DroidNet.Hosting.WinUI;
using DroidNet.Mvvm;
using DroidNet.Mvvm.Converters;
using Microsoft.UI.Dispatching;
using Microsoft.UI.Xaml;
using Moq;
using Oxygen.Editor.ContentBrowser;
using Oxygen.Editor.ContentBrowser.AssetIdentity;
using Oxygen.Editor.ContentBrowser.Importing;
using Oxygen.Editor.ContentPipeline;
using Oxygen.Editor.ContentPipeline.Cooking;
using Oxygen.Editor.ContentPipeline.Import;
using Oxygen.Editor.Projects;
using Oxygen.Editor.World.Cooking;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.World.Tests;

/// <summary>Checks that import review submits the visible selection through the shared cooking operation.</summary>
public sealed partial class ContentBrowserCookFeedbackTests
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
            ProjectId = Guid.NewGuid(), Name = "Import", Category = Category.Games,
            ProjectRoot = Path.Combine(Path.GetTempPath(), Guid.NewGuid().ToString("N")),
            AuthoringMounts = [new("Content", "Content")], LocalFolderMounts = [], Scenes = [],
        };
        projects.Activate(project);
        var source = new Uri("asset:///Content/SourceMedia/DCC/Crate/Crate.gltf");
        var request = new SceneImportRequest(project, Path.Combine(Path.GetTempPath(), "Crate.gltf"), "Crate", new("asset:///Content/Models"));
        var snapshot = new CookRunSnapshot
        {
            OperationId = Guid.NewGuid(), ProjectId = project.ProjectId, ProjectRoot = project.ProjectRoot,
            DisplayName = "Crate", State = CookRunState.Failed, CompletedAt = DateTimeOffset.UtcNow,
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

    /// <summary>Accepting the review submits its values once; cancelling starts no native operation.</summary>
    /// <param name="accept">Whether the user confirms the review.</param>
    /// <returns>The asynchronous command handoff test.</returns>
    [TestMethod]
    [DataRow(true)]
    [DataRow(false)]
    public Task ImportReviewUsesSharedCookingAndHonorsCancel(bool accept) => EnqueueAsync(async () =>
    {
        var projects = new ProjectContextService();
        var project = new ProjectContext
        {
            ProjectId = Guid.NewGuid(), Name = "Import", Category = Category.Games,
            ProjectRoot = Path.Combine(Path.GetTempPath(), Guid.NewGuid().ToString("N")),
            AuthoringMounts = [new("Content", "Content")], LocalFolderMounts = [], Scenes = [],
        };
        projects.Activate(project);
        SceneImportRequest? submitted = null;
        var pipeline = new Mock<IContentPipelineService>();
        _ = pipeline.Setup(value => value.ImportSourceAsync(It.IsAny<SceneImportRequest>(), It.IsAny<CancellationToken>()))
            .Callback<SceneImportRequest, CancellationToken>((request, _) => submitted = request)
            .ReturnsAsync(new ContentCookResult(Guid.NewGuid(), CookTargetKind.Asset, OperationStatus.Succeeded, [], [], Inspection: null, Validation: null));
        var locator = new Mock<IViewLocator>();
        _ = locator.Setup(value => value.ResolveView(It.IsAny<object>())).Returns(() => new SceneImportDialogView());
        var dialogs = new Mock<IDialogService>();
        _ = dialogs.Setup(value => value.ShowAsync(It.IsAny<DialogSpec>(), It.IsAny<CancellationToken>()))
            .Returns<DialogSpec, CancellationToken>(async (spec, cancellationToken) =>
            {
                _ = spec.PrimaryButtonText.Should().Be("Import");
                var view = (SceneImportDialogView)spec.Content!;
                AssertImportPlacement(view.ViewModel!);
                view.ViewModel!.Name = "ReviewedCrate";
                view.ViewModel.DestinationFolder = "/Content/Props";
                _ = (await spec.PrimaryAction!().ConfigureAwait(true)).Should().BeTrue();
                return accept ? DialogButton.Primary : DialogButton.Close;
            });
        var provider = new Mock<IContentBrowserAssetProvider>();
        using var browser = new AssetsViewModel(
            Mock.Of<Oxygen.Editor.ContentPipeline.Cooking.ICookRunService>(),
            new ViewModelToView(locator.Object),
            CreateSceneFolderImportState(projects),
            projects,
            Mock.Of<IProjectManagerService>(),
            Mock.Of<IAuthoringTargetResolver>(),
            pipeline.Object,
            provider.Object,
            Mock.Of<IOperationResultPublisher>(),
            Mock.Of<IStatusReducer>(),
            Mock.Of<DroidNet.Storage.IStorageProvider>(),
            new StrongReferenceMessenger(),
            dialogs.Object,
            Mock.Of<DroidNet.Aura.Windowing.IWindowManagerService>())
        {
            IsOperationResultVisible = true,
        };
        var source = Path.Combine(Path.GetTempPath(), "Crate.gltf");
        await browser.ImportSourceFileAsync(project, source).ConfigureAwait(true);
        pipeline.Verify(value => value.ImportSourceAsync(It.IsAny<SceneImportRequest>(), It.IsAny<CancellationToken>()), accept ? Times.Once() : Times.Never());
        provider.Verify(value => value.RefreshAsync(AssetBrowserFilter.Default, It.IsAny<CancellationToken>()), accept ? Times.Once() : Times.Never());
        if (accept)
        {
            _ = submitted.Should().NotBeNull();
            _ = submitted!.SourcePath.Should().Be(source);
            _ = submitted.Name.Should().Be("ReviewedCrate");
            _ = submitted.DestinationFolder.Should().Be(new Uri("asset:///Content/Props"));
            _ = browser.IsOperationResultVisible.Should().BeFalse();
        }
    });

    private static ContentBrowserState CreateSceneFolderImportState(IProjectContextService projects)
    {
        var state = new ContentBrowserState(projects);
        state.SetSelectedFolders(["/Content/Scenes"]);
        return state;
    }

    private static void AssertImportPlacement(SceneImportDialogViewModel model)
    {
        _ = model.DestinationFolder.Should().Be("/Content");
        _ = model.OutputLocations.Should().Contain("/Content/Materials/Crate").And.Contain("/Content/Geometry/Crate").And.Contain("/Content/Scenes/Crate");
    }
}
