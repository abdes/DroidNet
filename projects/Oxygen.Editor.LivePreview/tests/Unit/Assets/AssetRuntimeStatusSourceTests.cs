// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Editor.Assets.Runtime;
using Oxygen.Editor.LivePreview.Assets;
using Oxygen.Editor.LivePreview.Tests.TestSupport;
using Oxygen.Editor.Runtime.Engine;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.LivePreview.Tests.Assets;

/// <summary>Checks how the engine's content state and asset requests reach the asset services.</summary>
[TestClass]
[TestCategory("AssetValue Runtime Status")]
public sealed class AssetRuntimeStatusSourceTests
{
    private static readonly Guid ProjectId = Guid.NewGuid();

    [TestMethod]
    [DataRow(RuntimeContentState.Unmounted, AssetRuntimeContentState.Unmounted)]
    [DataRow(RuntimeContentState.Updating, AssetRuntimeContentState.Updating)]
    [DataRow(RuntimeContentState.Mounted, AssetRuntimeContentState.Mounted)]
    [DataRow(RuntimeContentState.Unavailable, AssetRuntimeContentState.Unavailable)]
    [DataRow(RuntimeContentState.Failed, AssetRuntimeContentState.Unavailable)]
    public async Task GetStatus_MapsTheContentState(RuntimeContentState state, AssetRuntimeContentState expected)
    {
        var engine = CreateEngine();
        await using var lifetime = engine.ConfigureAwait(false);
        engine.ContentStatus = engine.ContentStatus with { State = state, Reason = "Reported" };
        using var source = new AssetRuntimeStatusSource(engine);

        var status = source.GetStatus(ProjectId);

        _ = status.State.Should().Be(expected);
        _ = status.Reason.Should().Be("Reported");
    }

    [TestMethod]
    public async Task GetStatus_WithoutRun_IsUnavailableEvenWhenMounted()
    {
        var engine = CreateEngine();
        await using var lifetime = engine.ConfigureAwait(false);
        engine.World.AssetRequests = [Request(engine.World.RunId, ProjectId, "/Content/Materials/Red.omat", succeeded: false)];
        engine.ContentStatus = engine.ContentStatus with { RunId = Guid.Empty };
        using var source = new AssetRuntimeStatusSource(engine);

        var status = source.GetStatus(ProjectId);

        _ = status.State.Should().Be(AssetRuntimeContentState.Unavailable);
        _ = status.Roots.Should().BeEmpty();
        _ = status.Requests.Should().BeEmpty();
    }

    [TestMethod]
    public async Task GetStatus_ReportsTheMountedRoots()
    {
        var engine = CreateEngine();
        await using var lifetime = engine.ConfigureAwait(false);
        using var source = new AssetRuntimeStatusSource(engine);

        _ = source.GetStatus(ProjectId).Roots.Should().Equal("C:/Project/.cooked/Content");
    }

    [TestMethod]
    public async Task GetStatus_KeepsOnlyAssetRequestsOfTheCurrentRunAndProject()
    {
        var engine = CreateEngine();
        await using var lifetime = engine.ConfigureAwait(false);
        var run = engine.World.RunId;
        engine.World.AssetRequests =
        [
            Request(run, ProjectId, "/Content/Materials/Red.omat", succeeded: false, "Material rejected"),
            Request(Guid.NewGuid(), ProjectId, "/Content/Materials/OldRun.omat", succeeded: false),
            Request(run, Guid.NewGuid(), "/Content/Materials/OtherProject.omat", succeeded: false),
            new(new(Guid.NewGuid(), new(run, SceneId.New(), Guid.NewGuid(), Guid.NewGuid()) { ProjectId = ProjectId }, new RuntimeDeselectNode(NodeId.New())), 1, Succeeded: true),
        ];
        using var source = new AssetRuntimeStatusSource(engine);

        _ = source.GetStatus(ProjectId).Requests.Should().Equal(new AssetRuntimeRequest("/Content/Materials/Red.omat", Succeeded: false, "Material rejected"));
        _ = source.GetStatus(projectId: null).Requests.Should().BeEmpty();
    }

    [TestMethod]
    public async Task Changed_IsRaisedForEachEngineEventUntilDisposed()
    {
        var engine = CreateEngine();
        await using var lifetime = engine.ConfigureAwait(false);
        var source = new AssetRuntimeStatusSource(engine);
        var raised = 0;
        source.Changed += (_, _) => raised++;

        engine.RaiseContentStatusChanged();
        engine.RaiseStateChanged();
        engine.World.RaiseAssetStatusChanged();
        source.Dispose();
        engine.World.RaiseAssetStatusChanged();

        _ = raised.Should().Be(3);
    }

    private static FakeEngine CreateEngine()
    {
        var engine = new FakeEngine();
        engine.ContentStatus = new(engine.World.RunId, 1, RuntimeContentState.Mounted, [new("C:/Project/.cooked/Content", "Content")]);
        return engine;
    }

    private static RuntimeAssetRequestStatus Request(Guid runId, Guid projectId, string materialPath, bool? succeeded, string? reason = null)
        => new(
            new(Guid.NewGuid(), new(runId, SceneId.New(), Guid.NewGuid(), Guid.NewGuid()) { ProjectId = projectId }, new RuntimeSetMaterialOverride(NodeId.New(), "/Content/Mesh.ogeo", Guid.NewGuid(), new string('0', 64), materialPath, MaterialSlotAssignmentIntent.ObservedEdit)),
            1,
            succeeded,
            reason);
}
