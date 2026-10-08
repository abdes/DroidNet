// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using CommunityToolkit.Mvvm.Messaging;
using Moq;
using Oxygen.Editor.ContentBrowser.AssetIdentity;
using Oxygen.Editor.ContentBrowser.Messages;
using Oxygen.Editor.ContentPipeline;
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

    /// <summary>Identity-changing commands stay visible, unavailable and explained.</summary>
    /// <returns>The asynchronous availability check.</returns>
    [TestMethod]
    public async Task RelocationCommandsExplainWhyTheyAreUnavailable()
    {
        var (browser, state, _) = await CreateBrowserAsync().ConfigureAwait(false);
        using (browser)
        {
            state.PublishSelection([CreateAsset("/Content/Materials/Clay.omat.json", AssetKind.Material, descriptor: "C:/Project/Content/Materials/Clay.omat.json")]);
            _ = browser.RelocationCommand.CanExecute(parameter: null).Should().BeFalse();
            _ = AssetsViewModel.RenameToolTip.Should().StartWith("Rename (F2)").And.Contain(AssetsViewModel.RelocationUnavailableReason);
            _ = AssetsViewModel.DeleteToolTip.Should().Contain(AssetsViewModel.RelocationUnavailableReason);
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

    private static async Task<(AssetsViewModel Browser, ContentBrowserState State, IMessenger Messenger)> CreateBrowserAsync()
    {
        var projects = new ProjectContextService();
        projects.Activate(new()
        {
            ProjectId = Guid.NewGuid(),
            Name = "Commands",
            Category = Category.Games,
            ProjectRoot = "C:/Project",
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
            Mock.Of<IAssetShell>());
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
