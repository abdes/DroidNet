// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Moq;
using Oxygen.Editor.ContentBrowser.AssetIdentity;
using Oxygen.Editor.ContentPipeline.Inspection;
using Oxygen.Editor.ContentPipeline.Snapshots;
using Oxygen.Editor.ContentPipeline.Status;
using Oxygen.Editor.Projects;
using Oxygen.Managed.Assets.Catalog;

namespace Oxygen.Editor.ContentBrowser.Tests;

/// <summary>Checks background library inspection independently of ordinary status projection.</summary>
public sealed partial class ContentBrowserAssetProviderTests
{
    /// <summary>Rows are available before native inspection completes, then refresh once from the retained metadata.</summary>
    /// <returns>The asynchronous background-inspection regression.</returns>
    [TestMethod]
    public async Task LibraryMetadataRefreshDoesNotBlockRowsOrRepeatForUnchangedCatalog()
    {
        using var workspace = new TempWorkspace();
        var uri = new Uri("asset:///Content/Materials/Red.omat.json");
        WriteMaterial(workspace.SourcePath("Content/Materials/Red.omat.json"));
        var catalog = new TestProjectAssetCatalog([new AssetRecord(uri)]);
        var projects = CreateProjectContextService(workspace);
        projects.Activate(projects.ActiveProject! with { LocalFolderMounts = [new("Library", workspace.Root)] });
        var entered = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var release = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var updated = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var known = false;
        var statuses = new Mock<IAssetCookStatusReader>();
        var metadata = statuses.As<ICookedLibraryMetadataService>();
        _ = statuses.Setup(value => value.ReadAsync(It.IsAny<ProjectContext>(), It.IsAny<IReadOnlyList<Uri>>(), It.IsAny<CancellationToken>()))
            .ReturnsAsync(() => [new AssetCookStatus(uri, known ? AssetCookFreshness.Current : AssetCookFreshness.Unknown, HasPublishedOutput: true, HasVerifiedOutput: true, [], [], [])]);
        _ = metadata.Setup(value => value.RefreshLibraryMetadataAsync(It.IsAny<ProjectContext>(), It.IsAny<CancellationToken>()))
            .Returns(async (ProjectContext project, CancellationToken token) =>
            {
                _ = project.Should().BeSameAs(projects.ActiveProject);
                _ = entered.TrySetResult();
                await release.Task.WaitAsync(token).ConfigureAwait(false);
                known = true;
                return true;
            });
        var runtime = Oxygen.Testing.AssetStatusFixture.CreateUnavailableRuntime();
        await using var runtimeLifetime = runtime.ConfigureAwait(false);
        using var provider = new ContentBrowserAssetProvider(catalog, projects, new TestProjectCookScopeProvider(workspace), new AssetIdentityReducer(), statuses.Object, new CookDocumentRegistry(), EmptyCookRuns(), runtime);
        var observer = new Observer<IReadOnlyList<ContentBrowserAssetItem>>(rows =>
        {
            if (rows.Any(row => row.CookStatus?.Freshness == AssetCookFreshness.Current))
            {
                _ = updated.TrySetResult();
            }
        });
        provider.Items.Subscribe(observer, this.TestContext.CancellationToken);
        await provider.RefreshAsync(AssetBrowserFilter.Default, this.TestContext.CancellationToken).WaitAsync(TimeSpan.FromSeconds(5), this.TestContext.CancellationToken).ConfigureAwait(false);
        await entered.Task.WaitAsync(TimeSpan.FromSeconds(5), this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = updated.Task.IsCompleted.Should().BeFalse();
        release.SetResult();
        await updated.Task.WaitAsync(TimeSpan.FromSeconds(5), this.TestContext.CancellationToken).ConfigureAwait(false);
        await provider.RefreshAsync(AssetBrowserFilter.Default, this.TestContext.CancellationToken).ConfigureAwait(false);
        metadata.Verify(value => value.RefreshLibraryMetadataAsync(It.IsAny<ProjectContext>(), It.IsAny<CancellationToken>()), Times.Once);
    }
}
