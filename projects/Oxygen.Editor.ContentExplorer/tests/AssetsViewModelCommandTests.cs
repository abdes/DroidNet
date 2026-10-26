// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Moq;
using Oxygen.Editor.Assets;
using Oxygen.Editor.ContentExplorer.Panes.ContentFolders;
using Oxygen.Editor.ContentExplorer.Relocation;
using Oxygen.Editor.ContentPipeline;
using Oxygen.Editor.ContentPipeline.Relocation;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Projects.Testing;
using Oxygen.Editor.Scenes.Testing;
using Oxygen.Editor.Scenes.Views;
using Oxygen.Editor.Shell.Desktop;
using Oxygen.Editor.Shell.Testing;
using Oxygen.Editor.Textures;
using Oxygen.Editor.World;
using Oxygen.Managed.Assets;
using Oxygen.Managed.Core;
using Oxygen.Managed.Core.Diagnostics;
using Testably.Abstractions;
using Testably.Abstractions.Testing;

namespace Oxygen.Editor.ContentExplorer.Tests;

/// <summary>Checks which results commands are available for a selection and folder, and why the others are not.</summary>
[TestClass]
public sealed class AssetsViewModelCommandTests
{
    /// <summary>Commands follow the selection: batch cook needs every asset cookable; open and reveal need one asset.</summary>
    /// <returns>The asynchronous availability check.</returns>
    [TestMethod]
    public async Task SelectionDecidesCommandAvailability()
    {
        var (browser, state, _, session) = await CreateExplorerAsync().ConfigureAwait(false);
        using (session)
        using (browser)
        {
            _ = browser.CopyPathCommand.CanExecute(parameter: null).Should().BeFalse();
            _ = browser.OpenSelectedCommand.CanExecute(parameter: null).Should().BeFalse();
            _ = browser.CookSelectedAssetCommand.CanExecute(parameter: null).Should().BeFalse();
            _ = browser.CookSelectedToolTip.Should().StartWith("Select assets");

            var clay = CreateAsset("/Content/Materials/Clay.omat", AssetKind.Material, descriptor: FileSystemPath.From("C:/Project/Content/Materials/Clay.omat.json"));
            var bolt = CreateAsset("/Content/Geometry/Bolt.ogeo", AssetKind.Geometry, descriptor: FileSystemPath.From("C:/Project/Content/Geometry/Bolt.ogeo.json"));
            state.PublishSelection([clay, bolt]);
            _ = browser.CookSelectedAssetCommand.CanExecute(parameter: null).Should().BeTrue();
            _ = browser.CopyPathCommand.CanExecute(parameter: null).Should().BeTrue();
            _ = browser.OpenSelectedCommand.CanExecute(parameter: null).Should().BeFalse("Open acts on one asset");
            _ = browser.ShowInFileExplorerCommand.CanExecute(parameter: null).Should().BeFalse();

            var packaged = CreateAsset(EngineAssets.DefaultMaterial.VirtualPath, AssetKind.Material, descriptor: null) with { PrimaryState = AssetState.Packaged };
            state.PublishSelection([clay, packaged]);
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
        var root = Path.Combine(Path.GetTempPath(), "OxygenExplorerCommands", Guid.NewGuid().ToString("N"));
        var clayPath = Path.Combine(root, "Content", "Materials", "Clay.omat.json");
        _ = Directory.CreateDirectory(Path.GetDirectoryName(clayPath)!);
        await File.WriteAllTextAsync(clayPath, "{}").ConfigureAwait(false);
        var workflow = new Mock<IAssetRelocationWorkflow>();
        _ = workflow.Setup(value => value.PromptNameAsync("Rename", "Clay")).ReturnsAsync("Brick");
        var (browser, state, _, session) = await CreateExplorerAsync(root, workflow.Object).ConfigureAwait(false);
        try
        {
            using (session)
            using (browser)
            {
                var clay = CreateAsset("/Content/Materials/Clay.omat", AssetKind.Material, descriptor: FileSystemPath.From(clayPath));
                state.PublishSelection([clay]);
                _ = browser.RenameSelectedCommand.CanExecute(parameter: null).Should().BeTrue();
                _ = browser.RenameToolTip.Should().StartWith("Rename (F2)").And.Contain("Content that uses it is updated");
                _ = browser.DeleteSelectedCommand.CanExecute(parameter: null).Should().BeTrue();
                _ = browser.FindReferencesCommand.CanExecute(parameter: null).Should().BeTrue();
                _ = browser.PasteCommand.CanExecute(parameter: null).Should().BeFalse();

                browser.CopySelectedCommand.Execute(parameter: null);
                state.SetSelectedFolders([AssetPath.Parse("/Content/Materials")]);
                _ = browser.PasteCommand.CanExecute(parameter: null).Should().BeTrue();
                state.SetSelectedFolders([AssetPath.Parse("/Cooked/Content")]);
                _ = browser.PasteCommand.CanExecute(parameter: null).Should().BeFalse();

                state.PublishSelection([clay]);
                await browser.RenameSelectedCommand.ExecuteAsync(parameter: null).ConfigureAwait(false);
                workflow.Verify(value => value.RelocateAsync(
                    It.Is<AssetRelocationRequest>(request => request.Moves.Single().Source == AssetPath.Parse("/Content/Materials/Clay.omat.json")
                        && request.Moves.Single().Target == AssetPath.Parse("/Content/Materials/Brick.omat.json")),
                    "Rename"));

                var output = CreateAsset("/Content/Models/Robot/robot.gltf/Body.ogeo", AssetKind.Geometry, descriptor: null) with { ImportSource = AssetPath.Parse("/Content/Models/Robot/robot.gltf") };
                state.PublishSelection([output]);
                _ = browser.RenameSelectedCommand.CanExecute(parameter: null).Should().BeFalse();
                _ = browser.RenameToolTip.Should().Contain("imported model");
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

    /// <summary>A folder the sources tree copied or cut pastes as that folder, whatever the asset list copied before.</summary>
    /// <returns>The asynchronous paste check.</returns>
    [TestMethod]
    public async Task FolderFromTheSourcesTreePastesAsAFolder()
    {
        var root = Path.Combine(Path.GetTempPath(), "OxygenExplorerCommands", Guid.NewGuid().ToString("N"));
        var clayPath = Path.Combine(root, "Content", "Materials", "Clay.omat.json");
        _ = Directory.CreateDirectory(Path.GetDirectoryName(clayPath)!);
        _ = Directory.CreateDirectory(Path.Combine(root, "Content", "Props"));
        await File.WriteAllTextAsync(clayPath, "{}").ConfigureAwait(false);
        var workflow = new Mock<IAssetRelocationWorkflow>();
        var (browser, state, _, session) = await CreateExplorerAsync(root, workflow.Object).ConfigureAwait(false);
        try
        {
            using (session)
            using (browser)
            {
                state.PublishSelection([CreateAsset("/Content/Materials/Clay.omat", AssetKind.Material, descriptor: FileSystemPath.From(clayPath))]);
                browser.CopySelectedCommand.Execute(parameter: null);

                state.SetClipboard([AssetPath.Parse("/Content/Materials")], isCut: false);
                state.SetSelectedFolders([AssetPath.Parse("/Content/Props")]);
                await browser.PasteCommand.ExecuteAsync(parameter: null).ConfigureAwait(false);
                workflow.Verify(value => value.PasteAsync(It.Is<IReadOnlyList<AssetPath>>(items => items.SequenceEqual(new[] { AssetPath.Parse("/Content/Materials") })), move: false, AssetPath.Parse("/Content/Props")));

                _ = workflow.Setup(value => value.PasteAsync(It.IsAny<IReadOnlyList<AssetPath>>(), move: true, AssetPath.Parse("/Content/Props")))
                    .ReturnsAsync(new AssetRelocationOutcome(Succeeded: true, "Move", "Moved."));
                state.SetClipboard([AssetPath.Parse("/Content/Materials")], isCut: true);
                await browser.PasteCommand.ExecuteAsync(parameter: null).ConfigureAwait(false);
                workflow.Verify(value => value.PasteAsync(It.Is<IReadOnlyList<AssetPath>>(items => items.SequenceEqual(new[] { AssetPath.Parse("/Content/Materials") })), move: true, AssetPath.Parse("/Content/Props")));
                _ = state.Clipboard.Should().BeEmpty("cut items are pasted once");
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
        var (browser, state, folders, session) = await CreateExplorerAsync().ConfigureAwait(false);
        using (session)
        using (browser)
        {
            state.SetSelectedFolders([AssetPath.Parse("/Content/Materials")]);
            _ = browser.CreateFolderCommand.CanExecute(parameter: null).Should().BeTrue();
            browser.CreateFolderCommand.Execute(parameter: null);
            folders.Verify(pane => pane.CreateFolderAsync(AssetPath.Parse("/Content/Materials")), Times.Once);

            state.SetSelectedFolders([AssetPath.Parse("/Cooked/Content")]);
            _ = browser.CreateFolderCommand.CanExecute(parameter: null).Should().BeFalse();
            _ = browser.CreateFolderToolTip.Should().Contain("read-only");
        }
    }

    /// <summary>Only content cooks, so the cooked output commands are offered for content and not for the project's own files.</summary>
    /// <returns>The asynchronous availability check.</returns>
    [TestMethod]
    public async Task CookedOutputCommandsTargetContentOnly()
    {
        var (browser, state, _, session) = await CreateExplorerAsync().ConfigureAwait(false);
        using (session)
        using (browser)
        {
            state.SetSelectedFolders([AssetPath.Parse("/Content/Materials")]);
            _ = browser.InspectCookedOutputCommand.CanExecute(parameter: null).Should().BeTrue();
            _ = browser.ValidateCookedOutputCommand.CanExecute(parameter: null).Should().BeTrue();

            state.SetSelectedFolders([AssetPath.Parse("/project/Media")]);
            _ = browser.InspectCookedOutputCommand.CanExecute(parameter: null).Should().BeFalse();
            _ = browser.ValidateCookedOutputCommand.CanExecute(parameter: null).Should().BeFalse();
        }
    }

    /// <summary>
    /// Add to scene places the one selected imported model or scene in the open scene, after showing it (scene
    /// instances 6.1); it needs an open scene, and a scene is never placed in itself.
    /// </summary>
    /// <returns>The asynchronous check.</returns>
    [TestMethod]
    public async Task AddToScenePlacesTheSelectedSourceInTheOpenScene()
    {
        using var scene = TestScenes.Open(new Scene { Name = "Harbor" });
        var area = new TestDocumentArea();
        var views = new Mock<ISceneViews>();
        _ = views.Setup(port => port.AddInstanceAsync(It.IsAny<DroidNet.Documents.DocumentId>(), It.IsAny<AssetPath>())).ReturnsAsync(true);
        var (browser, state, _, session) = await CreateExplorerAsync(documents: area, sceneViews: views.Object).ConfigureAwait(false);
        using (session)
        using (browser)
        {
            var car = CreateAsset("/Content/Vehicles/Car.glb", AssetKind.Model, descriptor: null) with { ImportSource = AssetPath.Parse("/Content/Vehicles/Car.glb") };
            state.PublishSelection([car]);
            _ = browser.AddToSceneCommand.CanExecute(parameter: null).Should().BeFalse("no scene is open");

            area.Add(scene.Document);
            _ = browser.AddToSceneCommand.CanExecute(parameter: null).Should().BeTrue();
            await browser.AddToSceneCommand.ExecuteAsync(parameter: null).ConfigureAwait(false);
            views.Verify(port => port.AddInstanceAsync(scene.Document.Id, car.Identity), Times.Once);
            _ = area.ActiveDocument.Should().BeSameAs(scene.Document);

            state.PublishSelection([CreateAsset("/Content/Vehicles/Ship.fbx", AssetKind.Model, descriptor: null)]);
            _ = browser.AddToSceneCommand.CanExecute(parameter: null).Should().BeFalse("a model counts once it is imported");

            state.PublishSelection([CreateAsset(scene.Document.File.Asset.VirtualPath, AssetKind.Scene, descriptor: null)]);
            _ = browser.AddToSceneCommand.CanExecute(parameter: null).Should().BeFalse("a scene is never placed in itself");

            state.PublishSelection([CreateAsset("/Content/Watchtower.oscene", AssetKind.Scene, descriptor: null)]);
            _ = browser.AddToSceneCommand.CanExecute(parameter: null).Should().BeTrue();
        }
    }

    private static async Task<(AssetsViewModel browser, ContentExplorerState state, Mock<IContentFoldersPane> folders, TestSession session)> CreateExplorerAsync(
        string? root = null,
        IAssetRelocationWorkflow? relocation = null,
        TestDocumentArea? documents = null,
        ISceneViews? sceneViews = null)
    {
        // A test that names a folder works on the disk; the others in memory.
        var session = new TestSession(TestProject.Create(root is null ? new MockFileSystem() : new RealFileSystem(), root).Context with
        {
            Name = "Commands",
            AuthoringMounts = [new("Content", "Content"), new("Cooked", ".cooked")],
        });
        var state = new ContentExplorerState();
        var folders = new Mock<IContentFoldersPane>();
        var browser = new AssetsViewModel(
            Mock.Of<Oxygen.Editor.ContentPipeline.Cooking.IAutomaticCookService>(),
            new DroidNet.Mvvm.Converters.ViewModelToView(Mock.Of<DroidNet.Mvvm.IViewLocator>()),
            state,
            session,
            Mock.Of<IContentPipelineService>(),
            Mock.Of<IAssetFeed>(),
            Mock.Of<IOperationResultPublisher>(),
            Mock.Of<IStatusReducer>(),
            Mock.Of<DroidNet.Storage.IStorageProvider>(),
            folders.Object,
            Mock.Of<DroidNet.Aura.Dialogs.IDialogService>(),
            Mock.Of<DroidNet.Aura.Windowing.IWindowManagerService>(),
            Mock.Of<IDesktopIntegration>(),
            new TextureImport(new MockFileSystem()),
            Mock.Of<Oxygen.Editor.Models.IModelImportPrompt>(),
            relocation,
            documents: documents,
            sceneViews: sceneViews);
        await browser.OnNavigatedToAsync(null!, null!).ConfigureAwait(false);
        return (browser, state, folders, session);
    }

    private static AssetEntry CreateAsset(string path, AssetKind kind, FileSystemPath? descriptor)
        => new(
            AssetPath.Parse(path),
            Path.GetFileName(path),
            kind,
            AssetState.Descriptor,
            DerivedState: null,
            AssetRuntimeAvailability.Unknown,
            path,
            SourcePath: null,
            DescriptorPath: descriptor,
            CookedPath: null,
            AssetGuid: null,
            [],
            IsSelectable: true);
}
