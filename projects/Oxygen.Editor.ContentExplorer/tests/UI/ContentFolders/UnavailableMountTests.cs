// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Editor.ContentExplorer.Panes.ContentFolders;
using Oxygen.Editor.ContentExplorer.TestSupport;
using Oxygen.Editor.Projects.Mounts;
using Oxygen.Editor.Projects.Testing;
using Testably.Abstractions;
using static DroidNet.Tests.UiTestHosting;

namespace Oxygen.Editor.ContentExplorer.UI.Tests.ContentFolders;

/// <summary>
/// A project opens with mounts this machine cannot read: a placeholder no <c>.mounts.json</c> binds, and a committed
/// folder that is missing. The sources tree keeps both, dimmed with their state, and their menu binds or overrides them.
/// </summary>
[TestClass]
public sealed class UnavailableMountTests : DroidNet.Tests.VisualUserInterfaceTests
{
    public TestContext TestContext { get; set; } = null!;

    /// <summary>Unbound and missing mounts stay in the tree with their state, and their menu offers Bind or Override.</summary>
    /// <returns>The asynchronous real-browser workflow.</returns>
    [TestMethod]
    public Task UnboundAndMissingMountsStayInTheTreeWithTheirState() => EnqueueAsync(async () =>
    {
        using var fixture = new ExplorerRevealFixture();
        var info = TestProject.InfoOf(fixture.Session.Context);
        info.Mounts.Add(new ProjectMount("Studio"));
        info.Mounts.Add(new ProjectMount("Media", MountKind.Source, "../Missing-Media"));
        fixture.Session.ChangeContext(TestProject.Open(info, new RealFileSystem()).Context);

        await fixture.OpenAsync().ConfigureAwait(true);
        var root = fixture.Folders.ShownItems.OfType<ProjectRootTreeItemAdapter>().Single();
        var mounts = (await root.Children.ConfigureAwait(true)).OfType<UnavailableMountTreeItemAdapter>().ToDictionary(static mount => mount.Label, StringComparer.Ordinal);

        _ = mounts.Keys.Should().BeEquivalentTo("Studio", "Media");
        _ = mounts["Studio"].IsDimmed.Should().BeTrue();
        _ = mounts["Studio"].Annotation.Should().Be("Not bound");
        _ = mounts["Studio"].StateToolTip.Should().Contain(".mounts.json");
        _ = mounts["Media"].Annotation.Should().Be("Folder missing");
        _ = mounts["Media"].StateToolTip.Should().Contain("../Missing-Media");
        _ = (await mounts["Media"].Children.ConfigureAwait(true)).Should().BeEmpty();
        _ = fixture.Folders.BuildFolderMenu(mounts["Studio"]).Items.Select(static item => item.Text).Should().Contain(["Bind to a folder…", "Open Mounts…"]);
        _ = fixture.Folders.BuildFolderMenu(mounts["Media"]).Items.Select(static item => item.Text).Should().Contain("Override folder…");
        await WaitForRenderAsync().ConfigureAwait(true);
    });
}
