// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using DroidNet.Controls;
using Oxygen.Editor.ContentBrowser.ProjectExplorer;
using Oxygen.Editor.ContentBrowser.TestSupport;
using static DroidNet.Tests.UiTestHosting;

namespace Oxygen.Editor.ContentBrowser.UI.Tests.Navigation;

/// <summary>New Folder runs through the real browser, sources tree and disk.</summary>
[TestClass]
public sealed partial class BrowserFolderTests : DroidNet.Tests.VisualUserInterfaceTests
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
        using var fixture = new BrowserRevealFixture();
        await fixture.OpenAsync().ConfigureAwait(true);
        await fixture.NavigateHistoryFolderAsync("Materials", this.TestContext.CancellationToken).ConfigureAwait(true);
        ITreeItem? naming = null;
        fixture.Explorer.RenameRequested += (_, args) => naming = args.Item;
        var assets = (AssetsViewModel)fixture.Browser.RightPaneViewModel!;

        _ = assets.CreateFolderCommand.CanExecute(parameter: null).Should().BeTrue(fixture.Diagnostics);
        assets.CreateFolderCommand.Execute(parameter: null);
        for (var frame = 0; naming is null && frame < 50; frame++)
        {
            await WaitForRenderAsync().ConfigureAwait(true);
        }

        var folder = naming.Should().BeOfType<FolderTreeItemAdapter>().Subject;
        _ = folder.Label.Should().Be(ProjectLayoutViewModel.NewFolderName);
        _ = Directory.Exists(folder.Folder.Location).Should().BeTrue();
        _ = Path.GetFileName(Path.GetDirectoryName(folder.Folder.Location)).Should().Be("Materials");
        _ = fixture.Explorer.ShownItems.Should().Contain(folder);

        var renamed = await fixture.Explorer.CommitRenameAsync(folder, "Props").ConfigureAwait(true);
        _ = renamed.Succeeded.Should().BeTrue(renamed.ErrorMessage);
        _ = folder.Label.Should().Be("Props");
        _ = Path.GetFileName(folder.Folder.Location).Should().Be("Props");
        _ = Directory.Exists(folder.Folder.Location).Should().BeTrue();

        await File.WriteAllTextAsync(Path.Combine(folder.Folder.Location, "Crate.omat.json"), "{}", this.TestContext.CancellationToken).ConfigureAwait(true);
        var blocked = await fixture.Explorer.CommitRenameAsync(folder, "Crates").ConfigureAwait(true);
        _ = blocked.Succeeded.Should().BeFalse();
        _ = blocked.ErrorMessage.Should().Be(ProjectLayoutViewModel.FolderRenameUnavailableReason);
        _ = folder.Label.Should().Be("Props");
    });
}
