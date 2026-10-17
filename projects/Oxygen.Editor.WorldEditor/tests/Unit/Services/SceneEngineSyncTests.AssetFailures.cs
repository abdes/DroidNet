// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using DroidNet.Documents;
using Moq;
using Oxygen.Editor.Runtime.Engine;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Documents;
using Oxygen.Editor.World.Services;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.WorldEditor.Unit.Tests.Services;

public sealed partial class SceneEngineSyncTests
{
    [TestMethod]
    public async Task AssetFailure_PublishesCurrentOperationAndDropsStaleOrDeletedTargets()
    {
        var commands = CreateManagedWorld();
        var engine = new Mock<IEngineService>();
        _ = engine.SetupGet(value => value.State).Returns(EngineServiceState.Running);
        _ = engine.SetupGet(value => value.WorldCommands).Returns(commands.Object);
        RuntimeWorldRequest? captured = null;
        _ = commands.Setup(value => value.Execute(It.IsAny<RuntimeWorldRequest>(), It.IsAny<CancellationToken>()))
            .Returns((RuntimeWorldRequest request, CancellationToken _) =>
            {
                if (request.Command is RuntimeSetMaterialOverride)
                {
                    captured = request;
                }

                return new RuntimeCommandResult(request.OperationId, request.Target.RunId, RuntimeCommandStatus.Accepted);
            });
        var current = true;
        _ = commands.Setup(value => value.IsCurrentAssetFailure(It.IsAny<RuntimeAssetLoadFailedEventArgs>())).Returns(() => current);
        var results = new List<OperationResult>();
        var publisher = new Mock<IOperationResultPublisher>();
        _ = publisher.Setup(value => value.Publish(It.IsAny<OperationResult>())).Callback<OperationResult>(results.Add);
        using var sut = new SceneEngineSync(engine.Object, operationResults: publisher.Object);
        var scene = CreateScene();
        using var document = Register(sut, scene);
        scene = document.Scene;
        var node = AddCubeWithMissingMaterial(scene);
        _ = await sut.SyncSceneAsync(scene, this.TestContext.CancellationToken).ConfigureAwait(false);
        var failure = new RuntimeAssetLoadFailedEventArgs(captured!, 71, "Asset path could not be resolved");

        commands.Raise(value => value.AssetLoadFailed += null, failure);
        current = false;
        commands.Raise(value => value.AssetLoadFailed += null, failure);
        current = true;
        _ = scene.RootNodes.Remove(node);
        commands.Raise(value => value.AssetLoadFailed += null, failure);

        var result = results.Should().ContainSingle().Subject;
        _ = result.OperationId.Should().Be(failure.Request.OperationId);
        _ = result.AffectedScope.NodeId.Should().Be(node.Id);
        _ = result.AffectedScope.SceneId.Should().Be(scene.Id);
        _ = result.Diagnostics.Should().ContainSingle().Which.Domain.Should().Be(FailureDomain.LiveSync);
        _ = result.Message.Should().Be(failure.Message);
    }

    // A cube whose one material slot names a material that does not exist, so the engine reports the material's load.
    private static SceneNode AddCubeWithMissingMaterial(Scene scene)
    {
        var node = new SceneNode(scene) { Name = "Cube" };
        scene.RootNodes.Add(node);
        var target = new World.Slots.MaterialSlotTarget(new Uri("asset:///Engine/Generated/BasicShapes/Cube"), Guid.Parse("10000000-0000-0000-0000-000000000001"), new string('a', 64));
        var geometry = new GeometryComponent { Name = "Geometry", Geometry = new(target.GeometryUri) };
        geometry.OverrideSlots.Add(new World.Slots.MaterialsSlot { Target = target, Material = new(new Uri("asset:///Content/Materials/Missing.omat.json")) });
        node.Components.Add(geometry);
        return node;
    }
}
