// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Moq;
using Oxygen.Editor.Assets.Runtime;
using Oxygen.Editor.Runtime.Engine;
using Oxygen.Editor.World.Services;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.WorldEditor.Unit.Tests.Services;

/// <summary>Checks how the engine's content state and asset requests reach the asset services.</summary>
[TestClass]
[TestCategory("Asset Runtime Status")]
public sealed class AssetRuntimeStatusSourceTests
{
    private static readonly Guid RunId = Guid.NewGuid();
    private static readonly Guid ProjectId = Guid.NewGuid();

    private RuntimeContentSnapshot content = new(RunId, 1, RuntimeContentState.Mounted, [new("C:/Project/.cooked/Content", "Content")]);
    private IReadOnlyList<RuntimeAssetRequestStatus> requests = [];

    [TestMethod]
    [DataRow(RuntimeContentState.Unmounted, AssetRuntimeContentState.Unmounted)]
    [DataRow(RuntimeContentState.Updating, AssetRuntimeContentState.Updating)]
    [DataRow(RuntimeContentState.Mounted, AssetRuntimeContentState.Mounted)]
    [DataRow(RuntimeContentState.Unavailable, AssetRuntimeContentState.Unavailable)]
    [DataRow(RuntimeContentState.Failed, AssetRuntimeContentState.Unavailable)]
    public void GetStatus_MapsTheContentState(RuntimeContentState state, AssetRuntimeContentState expected)
    {
        var (engine, _) = this.CreateEngine();
        this.content = this.content with { State = state, Reason = "Reported" };
        using var source = new AssetRuntimeStatusSource(engine.Object);

        var status = source.GetStatus(ProjectId);

        _ = status.State.Should().Be(expected);
        _ = status.Reason.Should().Be("Reported");
    }

    [TestMethod]
    public void GetStatus_WithoutRun_IsUnavailableEvenWhenMounted()
    {
        var (engine, _) = this.CreateEngine();
        this.content = this.content with { RunId = Guid.Empty };
        this.requests = [Request(RunId, ProjectId, "/Content/Materials/Red.omat", succeeded: false)];
        using var source = new AssetRuntimeStatusSource(engine.Object);

        var status = source.GetStatus(ProjectId);

        _ = status.State.Should().Be(AssetRuntimeContentState.Unavailable);
        _ = status.Roots.Should().BeEmpty();
        _ = status.Requests.Should().BeEmpty();
    }

    [TestMethod]
    public void GetStatus_ReportsTheMountedRoots()
    {
        var (engine, _) = this.CreateEngine();
        using var source = new AssetRuntimeStatusSource(engine.Object);

        _ = source.GetStatus(ProjectId).Roots.Should().Equal("C:/Project/.cooked/Content");
    }

    [TestMethod]
    public void GetStatus_KeepsOnlyAssetRequestsOfTheCurrentRunAndProject()
    {
        var (engine, _) = this.CreateEngine();
        this.requests =
        [
            Request(RunId, ProjectId, "/Content/Materials/Red.omat", succeeded: false, "Material rejected"),
            Request(Guid.NewGuid(), ProjectId, "/Content/Materials/OldRun.omat", succeeded: false),
            Request(RunId, Guid.NewGuid(), "/Content/Materials/OtherProject.omat", succeeded: false),
            new(new(Guid.NewGuid(), new(RunId, SceneId.New(), Guid.NewGuid(), Guid.NewGuid()) { ProjectId = ProjectId }, new RuntimeDeselectNode(NodeId.New())), 1, Succeeded: true),
        ];
        using var source = new AssetRuntimeStatusSource(engine.Object);

        _ = source.GetStatus(ProjectId).Requests.Should().Equal(new AssetRuntimeRequest("/Content/Materials/Red.omat", false, "Material rejected"));
        _ = source.GetStatus(projectId: null).Requests.Should().BeEmpty();
    }

    [TestMethod]
    public void Changed_IsRaisedForEachEngineEventUntilDisposed()
    {
        var (engine, commands) = this.CreateEngine();
        var source = new AssetRuntimeStatusSource(engine.Object);
        var raised = 0;
        source.Changed += (_, _) => raised++;

        engine.Raise(value => value.ContentStatusChanged += null, new RuntimeContentChangedEventArgs(this.content));
        engine.Raise(value => value.StateChanged += null, new EngineStateChangedEventArgs(RunId, EngineServiceState.NoEngine, EngineServiceState.NoEngine));
        commands.Raise(value => value.AssetStatusChanged += null, EventArgs.Empty);
        source.Dispose();
        commands.Raise(value => value.AssetStatusChanged += null, EventArgs.Empty);

        _ = raised.Should().Be(3);
    }

    private static RuntimeAssetRequestStatus Request(Guid runId, Guid projectId, string materialPath, bool? succeeded, string? reason = null)
        => new(
            new(Guid.NewGuid(), new(runId, SceneId.New(), Guid.NewGuid(), Guid.NewGuid()) { ProjectId = projectId }, new RuntimeSetMaterialOverride(NodeId.New(), "/Content/Mesh.ogeo", Guid.NewGuid(), new string('0', 64), materialPath, MaterialSlotAssignmentIntent.ObservedEdit)),
            1,
            succeeded,
            reason);

    private (Mock<IEngineService> Engine, Mock<IRuntimeWorldCommands> Commands) CreateEngine()
    {
        var commands = new Mock<IRuntimeWorldCommands>();
        _ = commands.SetupGet(value => value.AssetRequests).Returns(() => this.requests);
        var engine = new Mock<IEngineService>();
        _ = engine.SetupGet(value => value.WorldCommands).Returns(commands.Object);
        _ = engine.SetupGet(value => value.ContentStatus).Returns(() => this.content);
        return (engine, commands);
    }
}
