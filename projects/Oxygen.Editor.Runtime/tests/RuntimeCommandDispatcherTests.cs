// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;
using AwesomeAssertions;
using Moq;
using Oxygen.Editor.Runtime.Engine;

namespace Oxygen.Editor.Runtime.Tests;

[TestClass]
public sealed class RuntimeCommandDispatcherTests
{
    public TestContext TestContext { get; set; }

    [TestMethod]
    public void UnavailableWorldAndInput_DoNotRequireNativeFacades()
    {
        var sut = new RuntimeCommandDispatcher();
        var operation = Guid.NewGuid();
        var world = sut.Execute(new RuntimeWorldRequest(operation, default, new RuntimeDetachGeometry(Guid.NewGuid())), this.TestContext.CancellationToken);
        var input = sut.Execute(new RuntimeInputRequest(operation, default, new RuntimeFocusLostEvent()), this.TestContext.CancellationToken);
        _ = world.Status.Should().Be(RuntimeCommandStatus.Unavailable);
        _ = input.Status.Should().Be(RuntimeCommandStatus.Unavailable);
        _ = world.OperationId.Should().Be(operation);
        _ = sut.RunId.Should().BeEmpty();
    }

    [TestMethod]
    public async Task SceneReplacement_RejectsOldMutationsAndLateNodeAcknowledgment()
    {
        var native = new Mock<IRuntimeCommandTransport>(MockBehavior.Strict);
        _ = native.Setup(value => value.ActivateSceneAsync(It.IsAny<string>())).ReturnsAsync(value: true);
        var nodeCompletion = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        _ = native.Setup(value => value.CreateNodeAsync(It.IsAny<RuntimeCreateNode>())).Returns(nodeCompletion.Task);
        var sut = Start(native.Object);
        var first = Scene(sut);
        _ = await sut.ActivateSceneAsync(Guid.NewGuid(), first, "First", this.TestContext.CancellationToken).ConfigureAwait(false);
        var request = new RuntimeWorldRequest(Guid.NewGuid(), first, new RuntimeCreateNode("Cube", Guid.NewGuid(), ParentId: null, InitializeWorldAsRoot: true));
        var pending = sut.CreateNodeAsync(request, this.TestContext.CancellationToken);

        _ = await sut.ActivateSceneAsync(Guid.NewGuid(), Scene(sut), "Second", this.TestContext.CancellationToken).ConfigureAwait(false);
        nodeCompletion.SetResult();
        var completion = await pending.ConfigureAwait(false);
        var mutation = sut.Execute(request with { Command = new RuntimeDetachGeometry(Guid.NewGuid()) }, this.TestContext.CancellationToken);

        _ = completion.Status.Should().Be(RuntimeCommandStatus.Rejected);
        _ = mutation.Status.Should().Be(RuntimeCommandStatus.Rejected);
        native.Verify(value => value.Execute(It.IsAny<RuntimeWorldRequest>()), Times.Never);
    }

    [TestMethod]
    public async Task RuntimeExit_EndsPendingAcknowledgmentsAndRejectsOldRunAfterRestart()
    {
        var native = new Mock<IRuntimeCommandTransport>();
        _ = native.Setup(value => value.ActivateSceneAsync(It.IsAny<string>())).ReturnsAsync(value: true);
        _ = native.Setup(value => value.CreateNodeAsync(It.IsAny<RuntimeCreateNode>())).Returns(new TaskCompletionSource().Task);
        var loop = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var sut = new RuntimeCommandDispatcher();
        _ = sut.BeginRun(native.Object, loop.Task);
        var first = Scene(sut);
        _ = await sut.ActivateSceneAsync(Guid.NewGuid(), first, "First", this.TestContext.CancellationToken).ConfigureAwait(false);
        var request = new RuntimeWorldRequest(Guid.NewGuid(), first, new RuntimeCreateNode("Cube", Guid.NewGuid(), ParentId: null, InitializeWorldAsRoot: true));
        var pending = sut.CreateNodeAsync(request, this.TestContext.CancellationToken);

        loop.SetResult();
        var result = await pending.WaitAsync(TimeSpan.FromSeconds(5), this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = sut.BeginRun(native.Object, new TaskCompletionSource().Task);
        var oldMutation = sut.Execute(request, this.TestContext.CancellationToken);

        _ = result.Status.Should().Be(RuntimeCommandStatus.Unavailable);
        _ = oldMutation.Status.Should().Be(RuntimeCommandStatus.Rejected);
        _ = oldMutation.RunId.Should().Be(first.RunId);
    }

    [TestMethod]
    public async Task CancellationBeforeDispatch_DoesNotCreateOrMutateNativeState()
    {
        var native = new Mock<IRuntimeCommandTransport>(MockBehavior.Strict);
        var sut = Start(native.Object);
        using var cancelled = new CancellationTokenSource();
        await cancelled.CancelAsync().ConfigureAwait(false);
        var target = Scene(sut);

        var activation = await sut.ActivateSceneAsync(Guid.NewGuid(), target, "Cancelled", cancelled.Token).ConfigureAwait(false);
        var mutation = sut.Execute(new RuntimeWorldRequest(Guid.NewGuid(), target, new RuntimeDetachGeometry(Guid.NewGuid())), cancelled.Token);
        var input = sut.Execute(new RuntimeInputRequest(Guid.NewGuid(), default, new RuntimeFocusLostEvent()), cancelled.Token);

        _ = activation.Status.Should().Be(RuntimeCommandStatus.Cancelled);
        _ = mutation.Status.Should().Be(RuntimeCommandStatus.Cancelled);
        _ = input.Status.Should().Be(RuntimeCommandStatus.Cancelled);
        native.VerifyAdd(value => value.AssetLoadFailed += It.IsAny<EventHandler<RuntimeAssetLoadFailedEventArgs>>(), Times.Once);
        native.VerifyNoOtherCalls();
    }

    [TestMethod]
    public async Task WorldPayloads_PreserveAuthoredIdentitiesAndNumericUnits()
    {
        RuntimeWorldCommand? dispatched = null;
        var native = new Mock<IRuntimeCommandTransport>();
        _ = native.Setup(value => value.ActivateSceneAsync(It.IsAny<string>())).ReturnsAsync(value: true);
        _ = native.Setup(value => value.Execute(It.IsAny<RuntimeWorldRequest>())).Callback<RuntimeWorldRequest>(value => dispatched = value.Command);
        var sut = Start(native.Object);
        var target = Scene(sut);
        _ = await sut.ActivateSceneAsync(Guid.NewGuid(), target, "Fixture", this.TestContext.CancellationToken).ConfigureAwait(false);
        var command = new RuntimeSetLocalTransform(Guid.NewGuid(), new Vector3(1, -2, 3), Quaternion.CreateFromYawPitchRoll(0.2f, -0.3f, 0.4f), new Vector3(2, 3, 4));
        var request = new RuntimeWorldRequest(Guid.NewGuid(), target, command);

        var result = sut.Execute(request, this.TestContext.CancellationToken);

        _ = result.Status.Should().Be(RuntimeCommandStatus.Accepted);
        _ = result.OperationId.Should().Be(request.OperationId);
        _ = result.RunId.Should().Be(target.RunId);
        _ = result.SceneTarget.Should().Be(target);
        _ = dispatched.Should().BeSameAs(command);
    }

    [TestMethod]
    [DataRow(RuntimeCommandStatus.Rejected)]
    [DataRow(RuntimeCommandStatus.Unavailable)]
    [DataRow(RuntimeCommandStatus.Failed)]
    [DataRow(RuntimeCommandStatus.Cancelled)]
    public async Task NativeFailures_ReturnOriginalCauseForWorldAndInput(RuntimeCommandStatus expected)
    {
        Exception failure = expected switch
        {
            RuntimeCommandStatus.Rejected => new ArgumentException("Rejected payload", nameof(expected)),
            RuntimeCommandStatus.Unavailable => new NotSupportedException("Missing capability"),
            RuntimeCommandStatus.Cancelled => new OperationCanceledException("Cancelled dispatch"),
            _ => new TimeoutException("Native timeout"),
        };
        var native = new Mock<IRuntimeCommandTransport>();
        _ = native.Setup(value => value.ActivateSceneAsync(It.IsAny<string>())).ReturnsAsync(value: true);
        _ = native.Setup(value => value.Execute(It.IsAny<RuntimeWorldRequest>())).Throws(failure);
        _ = native.Setup(value => value.ExecuteInput(It.IsAny<ulong>(), It.IsAny<RuntimeInputEvent>())).Throws(failure);
        var sut = Start(native.Object);
        var target = Scene(sut);
        _ = await sut.ActivateSceneAsync(Guid.NewGuid(), target, "Fixture", this.TestContext.CancellationToken).ConfigureAwait(false);
        sut.RegisterView(17, Guid.NewGuid(), Guid.NewGuid());

        var world = sut.Execute(new RuntimeWorldRequest(Guid.NewGuid(), target, new RuntimeDetachGeometry(Guid.NewGuid())), this.TestContext.CancellationToken);
        var input = sut.Execute(new RuntimeInputRequest(Guid.NewGuid(), sut.GetViewTarget(17)!.Value, new RuntimeFocusLostEvent()), this.TestContext.CancellationToken);

        _ = world.Status.Should().Be(expected);
        _ = input.Status.Should().Be(expected);
        _ = world.Exception.Should().BeSameAs(failure);
        _ = input.Exception.Should().BeSameAs(failure);
    }

    [TestMethod]
    public void ViewRecreation_RejectsOldGenerationEvenWhenNativeIdIsReused()
    {
        var native = new Mock<IRuntimeCommandTransport>(MockBehavior.Strict);
        _ = native.Setup(value => value.ExecuteInput(17, It.IsAny<RuntimeInputEvent>()));
        var sut = Start(native.Object);
        var document = Guid.NewGuid();
        var viewport = Guid.NewGuid();
        sut.RegisterView(17, document, viewport);
        var old = sut.GetViewTarget(17)!.Value;
        sut.UnregisterView(17);
        sut.RegisterView(17, document, viewport);
        var current = sut.GetViewTarget(17)!.Value;
        var input = new RuntimeMouseMotionEvent(new Vector2(3, -5), new Vector2(1280, 720), DateTime.UtcNow);

        var rejected = sut.Execute(new RuntimeInputRequest(Guid.NewGuid(), old, input), this.TestContext.CancellationToken);
        var accepted = sut.Execute(new RuntimeInputRequest(Guid.NewGuid(), current, input), this.TestContext.CancellationToken);

        _ = rejected.Status.Should().Be(RuntimeCommandStatus.Rejected);
        _ = accepted.Status.Should().Be(RuntimeCommandStatus.Accepted);
        _ = accepted.ViewTarget.Should().Be(current);
        _ = current.DocumentId.Should().Be(document);
        _ = current.ViewportId.Should().Be(viewport);
        native.Verify(value => value.ExecuteInput(17, input), Times.Once);
    }

    private static RuntimeCommandDispatcher Start(IRuntimeCommandTransport transport)
    {
        var sut = new RuntimeCommandDispatcher();
        _ = sut.BeginRun(transport, new TaskCompletionSource().Task);
        return sut;
    }

    private static RuntimeSceneTarget Scene(RuntimeCommandDispatcher dispatcher)
        => new(dispatcher.RunId, Guid.NewGuid(), Guid.NewGuid(), Guid.NewGuid());
}
