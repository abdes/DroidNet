// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;
using AwesomeAssertions;
using Moq;
using Oxygen.Editor.Runtime.Engine;
using Oxygen.Editor.World.Documents;
using Oxygen.Editor.World.Serialization;
using Oxygen.Editor.World.Services;

namespace Oxygen.Editor.WorldEditor.Unit.Tests.Services;

public sealed partial class SceneEngineSyncTests
{
    [TestMethod]
    public async Task Backdrop_TravelsInOneEnvironmentCommandWithItsCubemapsOnlyWhenUsed()
    {
        var commands = CreateManagedWorld();
        var engine = new Mock<IEngineService>();
        _ = engine.SetupGet(value => value.State).Returns(EngineServiceState.Running);
        _ = engine.SetupGet(value => value.WorldCommands).Returns(commands.Object);
        using var sut = new SceneEngineSync(engine.Object);
        var scene = CreateScene();
        _ = sut.RegisterDocument(scene, new SceneDocumentMetadata(scene.Id));
        _ = await sut.SyncSceneAsync(scene, this.TestContext.CancellationToken).ConfigureAwait(false);
        var sent = new List<RuntimeSetEnvironment>();
        _ = commands.Setup(value => value.Execute(It.Is<RuntimeWorldRequest>(request => request.Command is RuntimeSetEnvironment), It.IsAny<CancellationToken>()))
            .Returns((RuntimeWorldRequest request, CancellationToken _) =>
            {
                sent.Add((RuntimeSetEnvironment)request.Command);
                return new RuntimeCommandResult(request.OperationId, request.Target.RunId, RuntimeCommandStatus.Accepted, "Accepted");
            });

        // A solid color backdrop that does not light the scene: the cubemaps behind
        // the unused sources are not loaded.
        var environment = new SceneEnvironmentData
        {
            AtmosphereEnabled = false,
            Background = new() { Enabled = true, ColorRgb = new Vector3(0.25f, 0.5f, 0.75f) },
            SkySphere = new() { Enabled = false, Source = SkySphereSource.SolidColor, Cubemap = new Uri("asset:///Content/Sky.otex.json") },
            SkyLight = new() { Source = SkyLightSource.CapturedScene, Cubemap = new Uri("asset:///Content/Light.otex.json") },
        };
        scene.SetEnvironment(environment);

        var result = await sut.UpdateEnvironmentAsync(scene, environment, this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = result.Overall.Should().Be(SyncStatus.Accepted);
        _ = result.PerField.Keys.Should().Contain([nameof(SceneEnvironmentData.Background), nameof(SceneEnvironmentData.SkySphere), nameof(SceneEnvironmentData.SkyLight)]);
        var command = sent.Should().ContainSingle().Which;
        _ = command.Background.Should().Be(environment.Background);
        _ = command.SkySphere.Should().Be(environment.SkySphere);
        _ = command.SkyLight.Should().Be(environment.SkyLight);
        _ = command.SkySphereCubemap.Should().BeNull();
        _ = command.SkyLightCubemap.Should().BeNull();
    }
}
