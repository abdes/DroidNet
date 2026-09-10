// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;
using AwesomeAssertions;
using Moq;
using Oxygen.Editor.Runtime.Engine;
using Oxygen.Editor.World.Serialization;
using Oxygen.Editor.World.Services;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.World.SceneExplorer.Tests;

public sealed partial class SceneEngineSyncTests
{
    [TestMethod]
    [DataRow(RuntimeCommandStatus.Rejected, SyncStatus.Rejected)]
    [DataRow(RuntimeCommandStatus.Unavailable, SyncStatus.Unsupported)]
    [DataRow(RuntimeCommandStatus.Failed, SyncStatus.Failed)]
    public async Task BackgroundFailure_DoesNotBorrowAnotherEnvironmentFieldsSuccess(RuntimeCommandStatus nativeStatus, SyncStatus expected)
    {
        var commands = CreateManagedWorld();
        var engine = new Mock<IEngineService>();
        _ = engine.SetupGet(value => value.State).Returns(EngineServiceState.Running);
        _ = engine.SetupGet(value => value.WorldCommands).Returns(commands.Object);
        using var sut = new SceneEngineSync(engine.Object);
        var scene = CreateScene();
        _ = await sut.SyncSceneAsync(scene, this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = commands.Setup(value => value.Execute(It.Is<RuntimeWorldRequest>(request => request.Command is RuntimeSetBackgroundColor), It.IsAny<CancellationToken>()))
            .Returns((RuntimeWorldRequest request, CancellationToken _) => new RuntimeCommandResult(request.OperationId, request.Target.RunId, nativeStatus, "Background rejected", new NotSupportedException("Controlled background failure")));
        var environment = new SceneEnvironmentData { AtmosphereEnabled = false, BackgroundColor = new Vector3(0.25f, 0.5f, 0.75f) };
        scene.SetEnvironment(environment);

        var result = await sut.UpdateEnvironmentAsync(scene, environment, this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = result.Overall.Should().Be(expected);
        _ = result.PerField[nameof(SceneEnvironmentData.BackgroundColor)].Status.Should().Be(expected);
        _ = result.PerField[nameof(SceneEnvironmentData.BackgroundColor)].Code.Should().StartWith(DiagnosticCodes.LiveSyncPrefix + "ENVIRONMENT.Background.");
        _ = result.PerField[nameof(SceneEnvironmentData.AtmosphereEnabled)].Status.Should().Be(SyncStatus.Accepted);
        _ = result.PerField[nameof(SceneEnvironmentData.PostProcess)].Status.Should().Be(SyncStatus.Accepted);
        _ = scene.Environment.BackgroundColor.Should().Be(environment.BackgroundColor);
    }
}
