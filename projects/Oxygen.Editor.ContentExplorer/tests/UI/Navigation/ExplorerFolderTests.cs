// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using DroidNet.Controls;
using Oxygen.Editor.ContentExplorer.Panes.ContentFolders;
using Oxygen.Editor.ContentExplorer.TestSupport;
using static DroidNet.Tests.UiTestHosting;

namespace Oxygen.Editor.ContentExplorer.UI.Tests.Navigation;

/// <summary>New Folder runs through the real browser, sources tree and disk.</summary>
[TestClass]
public sealed partial class ExplorerFolderTests : DroidNet.Tests.VisualUserInterfaceTests
{
    public TestContext TestContext { get; set; } = null!;

    /// <summary>
    /// New Folder creates "New folder" in the current authoring folder, opens it for naming, renames it on disk, and
    /// refuses a rename once the folder holds an asset.
    /// </summary>
    /// <returns>The asynchronous real-browser workflow.</returns>
    [TestMethod]
    public Task NewFolderIsNamedInPlaceAndRenamedOnlyWhileEmpty() => EnqueueAsync(async () =>
    {
        using var fixture = new ExplorerRevealFixture();
        await fixture.OpenAsync().ConfigureAwait(true);
        await fixture.NavigateHistoryFolderAsync("Materials", this.TestContext.CancellationToken).ConfigureAwait(true);
        ITreeItem? naming = null;
        fixture.Folders.RenameRequested += (_, args) => naming = args.Item;
        var assets = (AssetsViewModel)fixture.Explorer.RightPaneViewModel!;

        _ = assets.CreateFolderCommand.CanExecute(parameter: null).Should().BeTrue(fixture.Diagnostics);
        assets.CreateFolderCommand.Execute(parameter: null);
        for (var frame = 0; naming is null && frame < 50; frame++)
        {
            await WaitForRenderAsync().ConfigureAwait(true);
        }

        var folder = naming.Should().BeOfType<FolderTreeItemAdapter>().Subject;
        _ = folder.Label.Should().Be(ContentFoldersViewModel.NewFolderName);
        _ = Directory.Exists(folder.Folder.Location).Should().BeTrue();
        _ = Path.GetFileName(Path.GetDirectoryName(folder.Folder.Location)).Should().Be("Materials");
        _ = fixture.Folders.ShownItems.Should().Contain(folder);

        var renamed = await fixture.Folders.CommitRenameAsync(folder, "Props").ConfigureAwait(true);
        _ = renamed.Succeeded.Should().BeTrue(renamed.ErrorMessage);
        _ = folder.Label.Should().Be("Props");
        _ = Path.GetFileName(folder.Folder.Location).Should().Be("Props");
        _ = Directory.Exists(folder.Folder.Location).Should().BeTrue();

        await File.WriteAllTextAsync(Path.Combine(folder.Folder.Location, "Crate.omat.json"), "{}", this.TestContext.CancellationToken).ConfigureAwait(true);
        var blocked = await fixture.Folders.CommitRenameAsync(folder, "Crates").ConfigureAwait(true);
        _ = blocked.Succeeded.Should().BeFalse();
        _ = blocked.ErrorMessage.Should().Be(ContentFoldersViewModel.FolderRenameUnavailableReason);
        _ = folder.Label.Should().Be("Props");
    });
}
