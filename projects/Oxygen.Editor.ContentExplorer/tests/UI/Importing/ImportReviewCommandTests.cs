// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json;
using AwesomeAssertions;
using DroidNet.Aura.Dialogs;
using DroidNet.Aura.Windowing;
using DroidNet.Mvvm;
using DroidNet.Mvvm.Converters;
using Microsoft.UI;
using Microsoft.UI.Xaml.Controls;
using Moq;
using Oxygen.Editor.Assets;
using Oxygen.Editor.ContentPipeline;
using Oxygen.Editor.ContentPipeline.Import;
using Oxygen.Editor.Models;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Projects.Testing;
using Oxygen.Editor.Shell.Desktop;
using Oxygen.Editor.Textures;
using Oxygen.Managed.Core;
using Oxygen.Managed.Core.Diagnostics;
using Testably.Abstractions.Testing;

namespace Oxygen.Editor.ContentExplorer.UI.Tests.Importing;

/// <summary>
/// The Content Explorer opens the Import model dialog for models, without a file picker of its own, and imports what
/// the dialog returns as one run (model editor LLD section 7).
/// </summary>
[TestClass]
public sealed class ImportReviewCommandTests : DroidNet.Tests.VisualUserInterfaceTests
{
    private static readonly AssetPath SelectedFolder = AssetPath.Parse("/Content/Scenes");

    [TestMethod]
    public Task ImportModel_OpensTheDialogInTheSelectedFolderAndImportsItsPlanAsOneRun() => EnqueueAsync(async () =>
    {
        var fixture = await Fixture.CreateAsync(pickedFile: null).ConfigureAwait(true);
        using var browser = fixture.Browser;
        using var session = fixture.Session;
        var settings = JsonSerializer.SerializeToElement(new Dictionary<string, object> { ["name"] = "Car", ["unit_policy"] = "preserve" });
        var run = new ImportRunSettings { ThreadPoolSize = 4 };
        fixture.Plan = new([
            new(@"D:\Sources\Car.fbx", "Car", AssetPath.Parse("/Content/Vehicles"), settings),
            new(@"D:\Sources\Truck.glb", "Truck", AssetPath.Parse("/Content/Vehicles"), settings),
        ], run);

        await browser.ImportModelCommand.ExecuteAsync(parameter: null).ConfigureAwait(true);

        fixture.Dialogs.Verify(value => value.PickFileAsync(It.IsAny<FilePickerSpec>(), It.IsAny<WindowId>(), It.IsAny<CancellationToken>()), Times.Never());
        _ = fixture.Prompted.Should().ContainSingle();
        _ = fixture.Prompted[0].DestinationFolder.Should().Be(SelectedFolder);
        _ = fixture.Prompted[0].Files.Should().BeEmpty("Import > Source model opens the dialog with no file");
        _ = fixture.Imported.Should().ContainSingle("the dialog's models import as one run");
        var (requests, imported) = fixture.Imported[0];
        _ = requests.Select(static request => request.Name).Should().Equal("Car", "Truck");
        _ = requests.Should().AllSatisfy(request => request.Settings!.Value.GetProperty("unit_policy").GetString().Should().Be("preserve"));
        _ = requests.Should().AllSatisfy(static request => request.Replacement.Should().BeNull());
        _ = imported.Should().BeSameAs(run);
        fixture.Feed.Verify(value => value.RefreshAsync(It.IsAny<CancellationToken>()), Times.Once());
    });

    [TestMethod]
    public Task ImportModel_CancelledDialogImportsNothing() => EnqueueAsync(async () =>
    {
        var fixture = await Fixture.CreateAsync(pickedFile: null).ConfigureAwait(true);
        using var browser = fixture.Browser;
        using var session = fixture.Session;

        await browser.ImportModelCommand.ExecuteAsync(parameter: null).ConfigureAwait(true);

        _ = fixture.Prompted.Should().ContainSingle();
        _ = fixture.Imported.Should().BeEmpty();
    });

    [TestMethod]
    [DataRow(true)]
    [DataRow(false)]
    public Task Import_PickedModelOpensTheDialogWithIt(bool picked) => EnqueueAsync(async () =>
    {
        var source = @"D:\External Sources\Crate.gltf";
        var fixture = await Fixture.CreateAsync(picked ? source : null).ConfigureAwait(true);
        using var browser = fixture.Browser;
        using var session = fixture.Session;

        await browser.ImportCommand.ExecuteAsync(parameter: null).ConfigureAwait(true);

        _ = fixture.Prompted.Should().HaveCount(picked ? 1 : 0);
        if (picked)
        {
            _ = fixture.Prompted[0].Files.Should().Equal(source);
        }
    });

    private sealed class Fixture
    {
        private Fixture(ProjectContext project)
        {
            this.Project = project;
        }

        public ProjectContext Project { get; }

        public Mock<IDialogService> Dialogs { get; } = new();

        public Mock<IAssetFeed> Feed { get; } = new();

        public List<ModelImportPromptRequest> Prompted { get; } = [];

        public List<(IReadOnlyList<SceneImportRequest> Requests, ImportRunSettings Run)> Imported { get; } = [];

        public ModelImportPlan? Plan { get; set; }

        public AssetsViewModel Browser { get; private set; } = null!;

        public TestSession Session { get; private set; } = null!;

        public static async Task<Fixture> CreateAsync(string? pickedFile)
        {
            var project = TestProject.Create(new MockFileSystem(), Path.Combine(Path.GetTempPath(), Guid.NewGuid().ToString("N"))).Context with { Name = "Import" };
            var fixture = new Fixture(project);
            await LoadTestContentAsync(new Grid()).ConfigureAwait(true);
            var owner = DroidNet.Tests.VisualUserInterfaceTestsApp.MainWindow;
            var managed = new Mock<IManagedWindow>();
            _ = managed.SetupGet(value => value.Window).Returns(owner);
            var windows = new Mock<IWindowManagerService>();
            _ = windows.SetupGet(value => value.ActiveWindow).Returns(managed.Object);
            _ = fixture.Dialogs.Setup(value => value.PickFileAsync(It.IsAny<FilePickerSpec>(), owner.AppWindow.Id, It.IsAny<CancellationToken>()))
                .ReturnsAsync(pickedFile);
            var prompt = new Mock<IModelImportPrompt>();
            _ = prompt.Setup(value => value.ShowAsync(It.IsAny<ModelImportPromptRequest>(), It.IsAny<CancellationToken>()))
                .Callback<ModelImportPromptRequest, CancellationToken>((request, _) => fixture.Prompted.Add(request))
                .Returns(() => Task.FromResult(fixture.Plan));
            var pipeline = new Mock<IContentPipelineService>();
            _ = pipeline.Setup(value => value.ImportSourcesAsync(It.IsAny<IReadOnlyList<SceneImportRequest>>(), It.IsAny<ImportRunSettings>(), It.IsAny<CancellationToken>()))
                .Callback<IReadOnlyList<SceneImportRequest>, ImportRunSettings, CancellationToken>((requests, run, _) => fixture.Imported.Add((requests, run)))
                .ReturnsAsync([new ContentCookResult(Guid.NewGuid(), CookTargetKind.Changes, OperationStatus.Succeeded, [], [], Inspection: null, Validation: null)]);
            var state = new ContentExplorerState();
            state.SetSelectedFolders([SelectedFolder]);
            fixture.Session = new TestSession(project);
            fixture.Browser = new AssetsViewModel(
                Mock.Of<Oxygen.Editor.ContentPipeline.Cooking.IAutomaticCookService>(),
                new ViewModelToView(Mock.Of<IViewLocator>()),
                state,
                fixture.Session,
                pipeline.Object,
                fixture.Feed.Object,
                Mock.Of<IOperationResultPublisher>(),
                Mock.Of<IStatusReducer>(),
                Mock.Of<DroidNet.Storage.IStorageProvider>(),
                Mock.Of<Oxygen.Editor.ContentExplorer.Panes.ContentFolders.IContentFoldersPane>(),
                fixture.Dialogs.Object,
                windows.Object,
                Mock.Of<IDesktopIntegration>(),
                new TextureImport(new MockFileSystem()),
                prompt.Object);
            return fixture;
        }
    }
}
