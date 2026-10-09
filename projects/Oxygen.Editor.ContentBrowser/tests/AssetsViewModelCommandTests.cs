// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using CommunityToolkit.Mvvm.Messaging;
using Moq;
using Oxygen.Editor.ContentBrowser.AssetIdentity;
using Oxygen.Editor.ContentBrowser.Messages;
using Oxygen.Editor.ContentBrowser.Relocation;
using Oxygen.Editor.ContentPipeline;
using Oxygen.Editor.ContentPipeline.Relocation;
using Oxygen.Editor.Projects;
using Oxygen.Editor.World;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.ContentBrowser.Tests;

/// <summary>Checks which results commands are available for a selection and folder, and why the others are not.</summary>
[TestClass]
public sealed class AssetsViewModelCommandTests
{
    /// <summary>Commands follow the selection: batch cook needs every asset cookable; open and reveal need one asset.</summary>
    /// <returns>The asynchronous availability check.</returns>
    [TestMethod]
    public async Task SelectionDecidesCommandAvailability()
    {
        var (browser, state, _) = await CreateBrowserAsync().ConfigureAwait(false);
        using (browser)
        {
            _ = browser.CopyPathCommand.CanExecute(parameter: null).Should().BeFalse();
            _ = browser.OpenSelectedCommand.CanExecute(parameter: null).Should().BeFalse();
            _ = browser.CookSelectedAssetCommand.CanExecute(parameter: null).Should().BeFalse();
            _ = browser.CookSelectedToolTip.Should().StartWith("Select assets");

            var clay = CreateAsset("/Content/Materials/Clay.omat.json", AssetKind.Material, descriptor: "C:/Project/Content/Materials/Clay.omat.json");
            var bolt = CreateAsset("/Content/Geometry/Bolt.ogeo.json", AssetKind.Geometry, descriptor: "C:/Project/Content/Geometry/Bolt.ogeo.json");
            state.PublishSelection([clay, bolt]);
            _ = browser.CookSelectedAssetCommand.CanExecute(parameter: null).Should().BeTrue();
            _ = browser.CopyPathCommand.CanExecute(parameter: null).Should().BeTrue();
            _ = browser.OpenSelectedCommand.CanExecute(parameter: null).Should().BeFalse("Open acts on one asset");
            _ = browser.ShowInFileExplorerCommand.CanExecute(parameter: null).Should().BeFalse();

            var builtin = CreateAsset("/Engine/Materials/Default.omat", AssetKind.Material, descriptor: null) with { PrimaryState = AssetState.Generated };
            state.PublishSelection([clay, builtin]);
            _ = browser.CookSelectedAssetCommand.CanExecute(parameter: null).Should().BeFalse("a batch cooks only when every asset has an authored source");

            state.PublishSelection([clay]);
            _ = browser.OpenSelectedCommand.CanExecute(parameter: null).Should().BeTrue();
            _ = browser.ShowInFileExplorerCommand.CanExecute(parameter: null).Should().BeTrue();
        }
    }

    /// <summary>Rename, move, copy and delete follow the selection, explain why they are unavailable, and request relocations.</summary>
    /// <returns>The asynchronous availability check.</returns>
    [TestMethod]
    public async Task RelocationCommandsFollowSelectionAndExplainWhy()
    {
        var root = Path.Combine(Path.GetTempPath(), "OxygenBrowserCommands", Guid.NewGuid().ToString("N"));
        var clayPath = Path.Combine(root, "Content", "Materials", "Clay.omat.json");
        _ = Directory.CreateDirectory(Path.GetDirectoryName(clayPath)!);
        await File.WriteAllTextAsync(clayPath, "{}").ConfigureAwait(false);
        var workflow = new Mock<IAssetRelocationWorkflow>();
        _ = workflow.Setup(value => value.PromptNameAsync("Rename", "Clay", false)).ReturnsAsync("Brick");
        var (browser, state, _) = await CreateBrowserAsync(root, workflow.Object).ConfigureAwait(false);
        try
        {
            using (browser)
            {
                var clay = CreateAsset("/Content/Materials/Clay.omat.json", AssetKind.Material, descriptor: clayPath);
                state.PublishSelection([clay]);
                _ = browser.RenameSelectedCommand.CanExecute(parameter: null).Should().BeTrue();
                _ = browser.RenameToolTip.Should().StartWith("Rename (F2)").And.Contain("Content that uses it is updated");
                _ = browser.DeleteSelectedCommand.CanExecute(parameter: null).Should().BeTrue();
                _ = browser.FindReferencesCommand.CanExecute(parameter: null).Should().BeTrue();
                _ = browser.PasteCommand.CanExecute(parameter: null).Should().BeFalse();

                browser.CopySelectedCommand.Execute(parameter: null);
                state.SetSelectedFolders(["/Content/Materials"]);
                _ = browser.PasteCommand.CanExecute(parameter: null).Should().BeTrue();
                state.SetSelectedFolders(["/Cooked/Content"]);
                _ = browser.PasteCommand.CanExecute(parameter: null).Should().BeFalse();

                state.PublishSelection([clay]);
                await browser.RenameSelectedCommand.ExecuteAsync(parameter: null).ConfigureAwait(false);
                workflow.Verify(value => value.RelocateAsync(
                    It.Is<AssetRelocationRequest>(request => request.Moves.Single().SourcePath == "/Content/Materials/Clay.omat.json"
                        && request.Moves.Single().TargetPath == "/Content/Materials/Brick.omat.json"),
                    "Rename"));

                var output = CreateAsset("/Content/Geometry/Robot/Body.ogeo", AssetKind.Geometry, descriptor: null) with { ImportSourceUri = new Uri("asset:///Content/SourceMedia/Robot/robot.gltf") };
                state.PublishSelection([output]);
                _ = browser.RenameSelectedCommand.CanExecute(parameter: null).Should().BeFalse();
                _ = browser.RenameToolTip.Should().Contain("Rename output group");
                _ = browser.DeleteToolTip.Should().Contain("Delete the model");

                var cooked = CreateAsset("/Cooked/Content/Materials/Clay.omat", AssetKind.Material, descriptor: null);
                state.PublishSelection([cooked]);
                _ = browser.CutSelectedCommand.CanExecute(parameter: null).Should().BeFalse();
                _ = browser.CutToolTip.Should().Contain("read-only");
            }
        }
        finally
        {
            Directory.Delete(root, recursive: true);
        }
    }

    /// <summary>New Folder works in authoring folders only, and asks the sources tree to create the folder.</summary>
    /// <returns>The asynchronous availability check.</returns>
    [TestMethod]
    public async Task NewFolderTargetsWritableFoldersOnly()
    {
        var (browser, state, messenger) = await CreateBrowserAsync().ConfigureAwait(false);
        using (browser)
        {
            CreateFolderRequestMessage? request = null;
            messenger.Register<CreateFolderRequestMessage>(this, (_, message) => request = message);

            state.SetSelectedFolders(["/Content/Materials"]);
            _ = browser.CreateFolderCommand.CanExecute(parameter: null).Should().BeTrue();
            browser.CreateFolderCommand.Execute(parameter: null);
            _ = request.Should().NotBeNull();
            _ = request!.ParentFolder.Should().Be("/Content/Materials");

            state.SetSelectedFolders(["/Cooked/Content"]);
            _ = browser.CreateFolderCommand.CanExecute(parameter: null).Should().BeFalse();
            _ = browser.CreateFolderToolTip.Should().Contain("read-only");
        }
    }

    private static async Task<(AssetsViewModel browser, ContentBrowserState state, IMessenger messenger)> CreateBrowserAsync(string root = "C:/Project", IAssetRelocationWorkflow? relocation = null)
    {
        var projects = new ProjectContextService();
        projects.Activate(new()
        {
            ProjectId = Guid.NewGuid(),
            Name = "Commands",
            Category = Category.Games,
            ProjectRoot = root,
            AuthoringMounts = [new("Content", "Content"), new("Cooked", ".cooked")],
            LocalFolderMounts = [],
            Scenes = [],
        });
        var state = new ContentBrowserState(projects);
        var messenger = new StrongReferenceMessenger();
        var browser = new AssetsViewModel(
            Mock.Of<Oxygen.Editor.ContentPipeline.Cooking.ICookRunService>(),
            new DroidNet.Mvvm.Converters.ViewModelToView(Mock.Of<DroidNet.Mvvm.IViewLocator>()),
            state,
            projects,
            Mock.Of<IProjectManagerService>(),
            Mock.Of<IAuthoringTargetResolver>(),
            Mock.Of<IContentPipelineService>(),
            Mock.Of<IContentBrowserAssetProvider>(),
            Mock.Of<IOperationResultPublisher>(),
            Mock.Of<IStatusReducer>(),
            Mock.Of<DroidNet.Storage.IStorageProvider>(),
            messenger,
            Mock.Of<DroidNet.Aura.Dialogs.IDialogService>(),
            Mock.Of<DroidNet.Aura.Windowing.IWindowManagerService>(),
            Mock.Of<IAssetShell>(),
            relocation);
        await browser.OnNavigatedToAsync(null!, null!).ConfigureAwait(false);
        return (browser, state, messenger);
    }

    private static ContentBrowserAssetItem CreateAsset(string path, AssetKind kind, string? descriptor)
        => new(
            new Uri("asset://" + path),
            Path.GetFileName(path),
            kind,
            AssetState.Descriptor,
            DerivedState: null,
            AssetRuntimeAvailability.Unknown,
            path,
            SourcePath: null,
            DescriptorPath: descriptor,
            CookedUri: null,
            CookedPath: null,
            AssetGuid: null,
            [],
            IsSelectable: true);
}
