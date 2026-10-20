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
    /// <summary>Unchanged refreshes reuse status, and new producer facts invalidate it without starting native inspection.</summary>
    /// <returns>The asynchronous background-inspection regression.</returns>
    [TestMethod]
    public async Task StatusRefreshUsesSnapshotsAndNeverStartsLibraryInspection()
    {
        using var workspace = new AssetWorkspace();
        var identity = AssetPath.Parse("/Content/Materials/Red.omat");
        WriteMaterial(workspace.SourcePath("Content/Materials/Red.omat.json"));
        var catalog = new TestProjectAssetCatalog([new AssetRecord(AssetPath.Parse("/Content/Materials/Red.omat.json"))]);
        var projects = workspace.ActivateProject();
        projects.Activate(projects.ActiveProject! with { LocalFolderMounts = [new("Library", workspace.Root)] });
        var updated = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var known = false;
        var statuses = new Mock<IAssetCookStatusReader>();
        _ = statuses.Setup(value => value.ReadAsync(It.IsAny<ProjectContext>(), It.IsAny<IReadOnlyList<AssetPath>>(), It.IsAny<CancellationToken>()))
            .ReturnsAsync(() => [new AssetCookStatus(identity, known ? AssetCookFreshness.Current : AssetCookFreshness.Unknown, HasPublishedOutput: true, OutputAvailability: CookedOutputAvailability.Present, [], [], [])]);
        using var provider = AssetFeeds.Create(catalog, projects, new WorkspaceCookScopeProvider(workspace), cookStatus: statuses.Object);
        var observer = new Observer<IReadOnlyList<AssetEntry>>(rows =>
        {
            if (rows.Any(row => row.CookStatus?.Freshness == AssetCookFreshness.Current))
            {
                _ = updated.TrySetResult();
            }
        });
        provider.Items.Subscribe(observer, this.TestContext.CancellationToken);
        await provider.RefreshAsync(this.TestContext.CancellationToken).WaitAsync(TimeSpan.FromSeconds(5), this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = updated.Task.IsCompleted.Should().BeFalse();
        await provider.RefreshAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        statuses.Verify(value => value.ReadAsync(It.IsAny<ProjectContext>(), It.IsAny<IReadOnlyList<AssetPath>>(), It.IsAny<CancellationToken>()), Times.Once);
        known = true;
        statuses.Raise(value => value.Changed += null, EventArgs.Empty);
        await updated.Task.WaitAsync(TimeSpan.FromSeconds(5), this.TestContext.CancellationToken).ConfigureAwait(false);
    }
}
