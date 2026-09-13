// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Moq;
using Oxygen.Editor.ContentBrowser.AssetIdentity;
using Oxygen.Editor.ContentBrowser.Materials;
using Oxygen.Editor.ContentPipeline.Snapshots;
using Oxygen.Editor.ContentPipeline.Status;
using Oxygen.Editor.Runtime.Engine;
using Oxygen.Managed.Assets.Catalog;

namespace Oxygen.Editor.ContentBrowser.Tests;

/// <summary>Checks native availability against current project, run, mount and asset outcomes.</summary>
public sealed partial class ContentBrowserAssetProviderTests
{
    /// <summary>Native events update every projection without rereading source or starting cooking.</summary>
    /// <returns>The asynchronous runtime-projection regression.</returns>
    [TestMethod]
    public async Task RuntimeAcknowledgementsAndAssetFailuresUpdateSharedStatusWithoutScanning()
    {
        using var workspace = new TempWorkspace();
        WriteMaterial(workspace.SourcePath("Content/Materials/Red.omat.json"));
        var uri = new Uri("asset:///Content/Materials/Red.omat.json");
        var context = CreateProjectContextService(workspace);
        var projectId = context.ActiveProject!.ProjectId;
        var runId = Guid.NewGuid();
        var content = new RuntimeContentSnapshot(runId, 1, RuntimeContentState.Unmounted, []);
        RuntimeAssetRequestStatus[] requests = [];
        var (engine, commands) = CreateRuntimeProjectionSource(() => content, () => requests);
        var reads = 0;
        var reader = new DelegateStatusReader((_, _, _) =>
        {
            reads++;
            return Task.FromResult<IReadOnlyList<AssetCookStatus>>([new(uri, AssetCookFreshness.Current, HasPublishedOutput: true, HasVerifiedOutput: true, [], [], [])]);
        });
        using var provider = new ContentBrowserAssetProvider(new TestProjectAssetCatalog([new AssetRecord(uri)]), context, new TestProjectCookScopeProvider(workspace), new AssetIdentityReducer(), reader, new CookDocumentRegistry(), EmptyCookRuns(), engine.Object);
        using var picker = new MaterialPickerService(provider);
        IReadOnlyList<ContentBrowserAssetItem> rows = [];
        IReadOnlyList<MaterialPickerResult> choices = [];
        provider.Items.Subscribe(new Observer<IReadOnlyList<ContentBrowserAssetItem>>(value => rows = value), this.TestContext.CancellationToken);
        picker.Results.Subscribe(new Observer<IReadOnlyList<MaterialPickerResult>>(value => choices = value), this.TestContext.CancellationToken);
        await provider.RefreshAsync(AssetBrowserFilter.Default, this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = rows.Single().PrimaryBadge.Should().Be("Cooked");
        var baselineReads = reads;
        content = content with { Revision = 2, State = RuntimeContentState.Mounted, Roots = [workspace.SourcePath(".cooked/Content")] };
        engine.Raise(value => value.ContentStatusChanged += null, new RuntimeContentChangedEventArgs(content));
        _ = rows.Single().PrimaryBadge.Should().Be("Ready");
        var request = new RuntimeWorldRequest(Guid.NewGuid(), new RuntimeSceneTarget(runId, Guid.NewGuid(), Guid.NewGuid(), Guid.NewGuid()) { ProjectId = projectId }, new RuntimeSetMaterialOverride(Guid.NewGuid(), 0, "/Content/Materials/Red.omat"));
        foreach (var (success, text) in new (bool?, string)[] { (null, "Updating preview"), (false, "Preview issue"), (true, "Ready") })
        {
            requests = [new(request, 9, success, success == false ? "Material rejected" : null)];
            commands.Raise(value => value.AssetStatusChanged += null, EventArgs.Empty);
            _ = rows.Single().PrimaryBadge.Should().Be(text);
            _ = choices.Single().StatusText.Should().Be(text);
            _ = rows.Single().CookStatus!.HasVerifiedOutput.Should().BeTrue();
        }

        requests = [new(request with { Target = request.Target with { ProjectId = Guid.NewGuid() } }, 10, Succeeded: false, "Other project")];
        commands.Raise(value => value.AssetStatusChanged += null, EventArgs.Empty);
        _ = rows.Single().PrimaryBadge.Should().Be("Ready");
        content = content with { Revision = 3, State = RuntimeContentState.Unavailable, Roots = [], Reason = "Native SDK mismatch" };
        engine.Raise(value => value.ContentStatusChanged += null, new RuntimeContentChangedEventArgs(content));
        _ = rows.Single().PrimaryBadge.Should().Be("Cooked");
        _ = rows.Single().PrimaryBadgeTooltip.Should().Contain("Native SDK mismatch");
        _ = reads.Should().Be(baselineReads);
    }

    private static (Mock<IEngineService> engine, Mock<IRuntimeWorldCommands> commands) CreateRuntimeProjectionSource(Func<RuntimeContentSnapshot> content, Func<RuntimeAssetRequestStatus[]> requests)
    {
        var commands = new Mock<IRuntimeWorldCommands>();
        _ = commands.SetupGet(value => value.AssetRequests).Returns(requests);
        var engine = new Mock<IEngineService>();
        _ = engine.SetupGet(value => value.WorldCommands).Returns(commands.Object);
        _ = engine.SetupGet(value => value.ContentStatus).Returns(content);
        return (engine, commands);
    }
}
