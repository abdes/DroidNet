// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Editor.Assets.Runtime;
using Oxygen.Editor.ContentPipeline.Snapshots;
using Oxygen.Editor.ContentPipeline.Status;
using Oxygen.Managed.Assets.Catalog;
using Oxygen.Managed.Core;
using Oxygen.Editor.Assets.TestSupport;

namespace Oxygen.Editor.Assets.Unit.Tests;

/// <summary>Checks preview availability against mounted roots and asset request outcomes.</summary>
public sealed partial class AssetFeedTests
{
    /// <summary>Preview changes update every projection without rereading source or starting cooking.</summary>
    /// <returns>The asynchronous runtime-projection regression.</returns>
    [TestMethod]
    public async Task RuntimeAcknowledgementsAndAssetFailuresUpdateSharedStatusWithoutScanning()
    {
        using var workspace = new AssetWorkspace();
        WriteMaterial(workspace.SourcePath("Content/Materials/Red.omat.json"));
        var identity = AssetPath.Parse("/Content/Materials/Red.omat");
        var context = workspace.OpenSession();
        var projectId = context.Context.ProjectId;
        var preview = new ControlledRuntimeStatus(new(AssetRuntimeContentState.Unmounted, [], []));
        var reads = 0;
        var reader = new DelegateStatusReader((_, _, _) =>
        {
            reads++;
            return Task.FromResult<IReadOnlyList<AssetCookStatus>>([new(identity, AssetCookFreshness.Current, HasPublishedOutput: true, OutputAvailability: CookedOutputAvailability.Present, [new(identity, identity, Oxygen.Editor.ContentPipeline.ContentCookAssetKind.Material, "Content", "/Content/Materials/Red.omat") { DescriptorRelativePath = "Materials/Red.omat" }], []) { OutputRoots = System.Collections.Immutable.ImmutableDictionary<string, FileSystemPath>.Empty.Add("Content", FileSystemPath.From(workspace.SourcePath(".cooked/Content"))) }]);
        });
        using var provider = AssetFeeds.Create(new TestProjectAssetCatalog([new AssetRecord(AssetPath.Parse("/Content/Materials/Red.omat.json"))]), context, new WorkspaceCookScopeProvider(workspace), cookStatus: reader, runtimeStatus: preview);
        IReadOnlyList<AssetEntry> rows = [];
        IReadOnlyList<AssetEntry> choices = [];
        provider.Items.Subscribe(new Observer<IReadOnlyList<AssetEntry>>(value => rows = value), this.TestContext.CancellationToken);
        provider.Assets(AssetRule.For(AssetKind.Material)).Subscribe(new Observer<IReadOnlyList<AssetEntry>>(value => choices = value), this.TestContext.CancellationToken);
        await provider.RefreshAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = rows.Single().PrimaryBadge.Should().Be("Cooked");
        _ = preview.RequestedProjects.Should().Contain(projectId);
        var baselineReads = reads;
        var mounted = new AssetRuntimeStatus(AssetRuntimeContentState.Mounted, [FileSystemPath.From(workspace.SourcePath(".cooked/Content"))], []);
        preview.Publish(mounted);
        _ = rows.Single().PrimaryBadge.Should().Be("Ready");
        foreach (var (success, text) in new (bool?, string)[] { (null, "Updating preview"), (false, "Preview issue"), (true, "Ready") })
        {
            preview.Publish(mounted with { Requests = [new(AssetPath.Parse("/Content/Materials/Red.omat"), success, success == false ? "Material rejected" : null)] });
            _ = rows.Single().PrimaryBadge.Should().Be(text);
            _ = AssetStatusText.ForEntry(choices.Single()).Should().Be(text);
            _ = rows.Single().CookStatus!.HasAvailableOutput.Should().BeTrue();
        }

        preview.Publish(mounted with { Requests = [new(AssetPath.Parse("/Content/Materials/Other.omat"), Succeeded: false, "Other asset")] });
        _ = rows.Single().PrimaryBadge.Should().Be("Ready");
        preview.Publish(new(AssetRuntimeContentState.Unavailable, [], [], "Native SDK mismatch"));
        _ = rows.Single().PrimaryBadge.Should().Be("Cooked");
        _ = rows.Single().PrimaryBadgeTooltip.Should().Contain("Native SDK mismatch");
        _ = reads.Should().Be(baselineReads);
    }

    private sealed class ControlledRuntimeStatus(AssetRuntimeStatus initial) : IAssetRuntimeStatusSource
    {
        private AssetRuntimeStatus current = initial;

        public event EventHandler? Changed;

        public List<Guid?> RequestedProjects { get; } = [];

        public AssetRuntimeStatus GetStatus(Guid? projectId)
        {
            this.RequestedProjects.Add(projectId);
            return this.current;
        }

        public void Publish(AssetRuntimeStatus status)
        {
            this.current = status;
            this.Changed?.Invoke(this, EventArgs.Empty);
        }
    }
}
