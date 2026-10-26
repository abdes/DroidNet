// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Moq;
using Oxygen.Editor.ContentPipeline.Snapshots;
using Oxygen.Editor.ContentPipeline.Status;
using Oxygen.Editor.Projects;
using Oxygen.Managed.Assets.Catalog;
using Oxygen.Managed.Core;
using Oxygen.Editor.Assets.TestSupport;

namespace Oxygen.Editor.Assets.Unit.Tests;

/// <summary>Checks that status projection reuses status snapshots instead of inspecting libraries.</summary>
public sealed partial class AssetFeedTests
{
    /// <summary>Unchanged refreshes reuse status without starting native inspection.</summary>
    /// <returns>The asynchronous background-inspection regression.</returns>
    [TestMethod]
    public async Task StatusRefreshUsesSnapshotsAndNeverStartsLibraryInspection()
    {
        using var workspace = new AssetWorkspace();
        var identity = AssetPath.Parse("/Content/Materials/Red.omat");
        WriteMaterial(workspace.SourcePath("Content/Materials/Red.omat.json"));
        var catalog = new TestProjectAssetCatalog([new AssetRecord(AssetPath.Parse("/Content/Materials/Red.omat.json"))]);
        var projects = workspace.OpenSession();
        projects.ChangeContext(projects.Context with { LocalFolderMounts = [new("Library", workspace.Root)] });
        var statuses = new Mock<IAssetCookStatusReader>();
        _ = statuses.Setup(value => value.ReadAsync(It.IsAny<ProjectContext>(), It.IsAny<IReadOnlyList<AssetPath>>(), It.IsAny<CancellationToken>()))
            .ReturnsAsync(() => [new AssetCookStatus(identity, AssetCookFreshness.Unknown, HasPublishedOutput: true, OutputAvailability: CookedOutputAvailability.Present, [], [])]);
        using var provider = AssetFeeds.Create(catalog, projects, new WorkspaceCookScopeProvider(workspace), cookStatus: statuses.Object);
        await provider.RefreshAsync(this.TestContext.CancellationToken).WaitAsync(TimeSpan.FromSeconds(5), this.TestContext.CancellationToken).ConfigureAwait(false);
        await provider.RefreshAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        statuses.Verify(value => value.ReadAsync(It.IsAny<ProjectContext>(), It.IsAny<IReadOnlyList<AssetPath>>(), It.IsAny<CancellationToken>()), Times.Once);
    }
}
