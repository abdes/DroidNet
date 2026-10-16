// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Moq;
using Oxygen.Editor.Assets.TestSupport;
using Oxygen.Editor.ContentPipeline.Authoring;
using Oxygen.Editor.ContentPipeline.Relocation;
using Oxygen.Editor.ContentPipeline.Snapshots;
using Oxygen.Managed.Assets.Catalog;

namespace Oxygen.Editor.Assets.Unit.Tests;

/// <summary>Files the editor changes are read again when their service reports them, before the file watcher.</summary>
public sealed partial class AssetFeedTests
{
    /// <summary>A relocation's or a new script's report makes the feed read the catalog again.</summary>
    /// <param name="fromAuthoring">Whether script authoring reports the change, rather than the relocation service.</param>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    [DataRow(false)]
    [DataRow(true)]
    public async Task ReportedFileChangeReadsTheCatalogAgain(bool fromAuthoring)
    {
        using var workspace = new AssetWorkspace();
        WriteMaterial(workspace.SourcePath("Content/Materials/Red.omat.json"));
        var red = new AssetRecord(new Uri("asset:///Content/Materials/Red.omat.json"));
        var blue = new AssetRecord(new Uri("asset:///Content/Materials/Blue.omat.json"));
        var catalog = new TestProjectAssetCatalog([red]);
        var relocation = new Mock<IAssetRelocationService>();
        var authoring = new Mock<IScriptAssetAuthoring>();
        using var provider = new AssetFeed(catalog, workspace.ActivateProject(), new WorkspaceCookScopeProvider(workspace), new EmptyCookStatusReader(), new CookDocumentRegistry(), EmptyCookRuns(), AssetStatusFixture.CreateUnavailableRuntimeStatus(), relocation.Object, authoring.Object);
        IReadOnlyList<AssetEntry> rows = [];
        provider.Items.Subscribe(new Observer<IReadOnlyList<AssetEntry>>(value => rows = value), this.TestContext.CancellationToken);
        await provider.RefreshAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        WriteMaterial(workspace.SourcePath("Content/Materials/Blue.omat.json"));
        catalog.SetRecordsOnRefresh([red, blue]);

        var created = AssetFilesChangedEventArgs.ForCreated([workspace.SourcePath("Content/Materials/Blue.omat.json")], foldersCreated: false);
        if (fromAuthoring)
        {
            authoring.Raise(value => value.FilesChanged += null, created);
        }
        else
        {
            relocation.Raise(value => value.FilesChanged += null, created);
        }

        await provider.RefreshAsync(this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = catalog.RefreshCount.Should().Be(2);
        _ = rows.Select(static row => row.IdentityUri).Should().Contain(blue.Uri);
    }
}
