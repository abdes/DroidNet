// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Reactive.Subjects;
using AwesomeAssertions;
using Oxygen.Editor.Assets;
using Oxygen.Editor.ContentExplorer.Panes.Assets.Layouts;
using Oxygen.Editor.ContentExplorer.Panes.ContentFolders;
using Oxygen.Managed.Core;
using static DroidNet.Tests.UiTestHosting;
using static Oxygen.Editor.ContentExplorer.TestSupport.ExplorerTestData;

namespace Oxygen.Editor.ContentExplorer.UI.Tests.Navigation;

/// <summary>
/// A container is one asset, its source file: the browser lists neither a model's products nor an input source's
/// actions and contexts, in the project or in a pak. Pickers and references reach them by identity.
/// </summary>
[TestClass]
public sealed partial class ExplorerContainerTests : DroidNet.Tests.VisualUserInterfaceTests
{
    public TestContext TestContext { get; set; } = null!;

    /// <summary>Both layouts list the model and the input source, and none of their products.</summary>
    /// <returns>The asynchronous listing test.</returns>
    [TestMethod]
    public Task ContainerProductsAreNotListed() => EnqueueAsync(async () =>
    {
        var model = CreateNavigationAsset("/Content/Models/Ship/Ship.glb", AssetKind.ForeignSource);
        var input = CreateNavigationAsset("/Content/Input/Player.input", AssetKind.InputFile);
        var material = CreateNavigationAsset("/Content/Models/Ship/Ship.glb/Paint.omat", AssetKind.Material);
        var action = CreateNavigationAsset("/Content/Input/Player.input/Jump.oiact", AssetKind.InputAction);
        var packed = CreateNavigationAsset("/Kit/Models/Ship.glb/Hull.ogeo", AssetKind.Geometry);
        using var updates = new BehaviorSubject<IReadOnlyList<AssetEntry>>([model, material, input, action, packed]);
        var provider = CreateQueryProvider(updates);
        var projects = CreateQueryProject();
        var state = new ContentExplorerState();
        using var list = new ListLayoutViewModel(provider.Object, projects, state, CreateStatusHosting());
        using var tiles = new TilesLayoutViewModel(provider.Object, projects, state, CreateStatusHosting());

        await list.OnNavigatedToAsync(null!, null!).ConfigureAwait(true);
        await tiles.OnNavigatedToAsync(null!, null!).ConfigureAwait(true);

        _ = list.Assets.Select(static row => row.Item.Identity).Should().BeEquivalentTo([model.Identity, input.Identity]);
        _ = tiles.Assets.Select(static row => row.Item.Identity).Should().BeEquivalentTo([model.Identity, input.Identity]);
    });

    /// <summary>A pak's folder tree shows no folder for a container's segment.</summary>
    [TestMethod]
    public void PakTreeHasNoContainerFolders()
    {
        var pak = new PakMountTreeItemAdapter("Kit", FileSystemPath.From("C:/Kit.pak"), []);

        var folders = CookedFolderTreeItemAdapter.Build(pak, ["Models/Ship.glb/Hull.ogeo", "Input/Player.input/Jump.oiact", "Props/Box.ogeo"]);

        _ = folders.Select(static folder => folder.Label).Should().Equal("Props");
    }
}
