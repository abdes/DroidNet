// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;
using AwesomeAssertions;
using Moq;
using Oxygen.Editor.Runtime.Engine;

namespace Oxygen.Editor.World.SceneExplorer.Tests;

public sealed partial class ViewportCameraControlModeTests
{
    [TestMethod]
    [DataRow(RuntimeCommandStatus.Accepted, 0)]
    [DataRow(RuntimeCommandStatus.Cancelled, 0)]
    [DataRow(RuntimeCommandStatus.Rejected, 1)]
    [DataRow(RuntimeCommandStatus.Unavailable, 1)]
    [DataRow(RuntimeCommandStatus.Failed, 1)]
    public void ManagedInputSubstitute_ForwardsCapturedTargetAndReportsFailureOnce(RuntimeCommandStatus status, int diagnosticCount)
    {
        var input = new Mock<IRuntimeInputCommands>(MockBehavior.Strict);
        var requests = new List<RuntimeInputRequest>();
        _ = input.Setup(value => value.Execute(It.IsAny<RuntimeInputRequest>(), It.IsAny<CancellationToken>()))
            .Returns((RuntimeInputRequest request, CancellationToken _) =>
            {
                requests.Add(request);
                return new RuntimeCommandResult(request.OperationId, request.Target.RunId, status, "Controlled input result");
            });
        var engine = new Mock<IEngineService>(MockBehavior.Strict);
        _ = engine.SetupGet(value => value.InputCommands).Returns(input.Object);
        var results = new CapturingOperationResultPublisher();
        using var sut = CreateViewportViewModel(engine.Object, results);
        var target = new RuntimeViewTarget(Guid.NewGuid(), sut.DocumentId, sut.ViewportId, Guid.NewGuid(), 43);
        var motion = new RuntimeMouseMotionEvent(new Vector2(2, -3), new Vector2(2560, 1440), DateTime.UtcNow);

        sut.ForwardInput(target, motion);
        sut.ForwardInput(target, motion);

        _ = requests.Should().HaveCount(2);
        _ = requests.Should().OnlyContain(value => value.Target == target && value.Input == motion);
        _ = results.Published.Should().HaveCount(diagnosticCount);
        if (diagnosticCount != 0)
        {
            _ = results.Published[0].OperationKind.Should().Be("Runtime.Input.Dispatch");
        }
    }
}
