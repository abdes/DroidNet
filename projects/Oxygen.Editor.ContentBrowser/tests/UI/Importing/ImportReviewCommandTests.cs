// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using CommunityToolkit.Mvvm.Messaging;
using DroidNet.Aura.Dialogs;
using DroidNet.Aura.Windowing;
using DroidNet.Mvvm;
using DroidNet.Mvvm.Converters;
using Microsoft.UI;
using Microsoft.UI.Xaml.Controls;
using Moq;
using Oxygen.Editor.ContentBrowser;
using Oxygen.Editor.ContentBrowser.AssetIdentity;
using Oxygen.Editor.ContentBrowser.Importing;
using Oxygen.Editor.ContentPipeline;
using Oxygen.Editor.ContentPipeline.Cooking;
using Oxygen.Editor.ContentPipeline.Import;
using Oxygen.Editor.Projects;
using Oxygen.Editor.World;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.ContentBrowser.UI.Tests.Importing;

[TestClass]
public sealed class ImportReviewCommandTests : DroidNet.Tests.VisualUserInterfaceTests
{
    /// <summary>Accepting the review submits its values once; cancelling starts no native operation.</summary>
    /// <param name="selectSource">Whether the source picker returns a file or is cancelled.</param>
    /// <param name="accept">Whether the user confirms the review.</param>
    /// <returns>The asynchronous command handoff test.</returns>
    [TestMethod]
    [DataRow(true, true)]
    [DataRow(true, false)]
    [DataRow(false, false)]
    public Task ImportReviewUsesSharedCookingAndHonorsCancel(bool selectSource, bool accept) => EnqueueAsync(async () =>
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
        SceneImportRequest? submitted = null;
        var pipeline = new Mock<IContentPipelineService>();
        _ = pipeline.Setup(value => value.ImportSourceAsync(It.IsAny<SceneImportRequest>(), It.IsAny<CancellationToken>()))
            .Callback<SceneImportRequest, CancellationToken>((request, _) => submitted = request)
            .ReturnsAsync(new ContentCookResult(Guid.NewGuid(), CookTargetKind.Asset, OperationStatus.Succeeded, [], [], Inspection: null, Validation: null));
        var locator = new Mock<IViewLocator>();
        _ = locator.Setup(value => value.ResolveView(It.IsAny<object>())).Returns(() => new SceneImportDialogView());
        var source = @"D:\External Sources\Crate.gltf";
        await LoadTestContentAsync(new Grid()).ConfigureAwait(true);
        var owner = DroidNet.Tests.VisualUserInterfaceTestsApp.MainWindow;
        var managed = new Mock<IManagedWindow>();
        _ = managed.SetupGet(value => value.Window).Returns(owner);
        var windows = new Mock<IWindowManagerService>();
        _ = windows.SetupGet(value => value.ActiveWindow).Returns(managed.Object);
        var dialogs = CreateDialogs(project, source, selectSource, accept);
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
            windows.Object)
        {
            IsOperationResultVisible = true,
        };
        await browser.ImportCommand.ExecuteAsync(parameter: null).ConfigureAwait(true);
        dialogs.Verify(value => value.ShowAsync(It.IsAny<DialogSpec>(), It.IsAny<CancellationToken>()), selectSource ? Times.Once() : Times.Never());
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

    private static Mock<IDialogService> CreateDialogs(ProjectContext project, string source, bool selectSource, bool accept)
    {
        var dialogs = new Mock<IDialogService>();
        var ownerId = DroidNet.Tests.VisualUserInterfaceTestsApp.MainWindow.AppWindow.Id;
        _ = dialogs.Setup(value => value.PickFileAsync(It.IsAny<FilePickerSpec>(), ownerId, It.IsAny<CancellationToken>()))
            .Callback<FilePickerSpec, WindowId, CancellationToken>((spec, _, _) =>
            {
                _ = spec.SettingsIdentifier.Should().Be("Oxygen.ImportAsset");
                _ = spec.SuggestedStartFolder.Should().Be(project.ProjectRoot);
                _ = spec.FileTypeChoices.Keys.Should().Equal("Supported assets", "3D models", "Textures");
                _ = spec.FileTypeChoices["3D models"].Should().Equal(".gltf", ".glb", ".fbx");
                _ = spec.FileTypeChoices["Textures"].Should().NotBeEmpty();
            })
            .ReturnsAsync(selectSource ? source : null);
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
        return dialogs;
    }

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
