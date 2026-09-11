// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Moq;
using Oxygen.Editor.Schemas;
using Oxygen.Editor.World.Documents;
using Oxygen.Editor.WorldEditor.Documents.Commands;

namespace Oxygen.Editor.World.SceneExplorer.Tests;

/// <summary>Verifies scene replacement waits for admitted edits and retires old callbacks.</summary>
public sealed partial class SceneDocumentCommandServiceTests
{
    /// <summary>A pending real scene command drains before replacement and retired contexts cannot edit again.</summary>
    /// <returns>The test task.</returns>
    [TestMethod]
    public async Task SceneReplacementDrainsCommandsAndRejectsOldContextEdits()
    {
        var fixture = CreateFixture();
        var scene = CreateScene();
        var context = CreateContext(scene);
        var native = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        _ = fixture.Sync.Setup(value => value.CreateNodeAsync(It.IsAny<SceneNode>(), parentGuid: null)).Returns(native.Task);
        var create = fixture.Sut.CreatePrimitiveAsync(context, "Cube");
        var replacing = SceneAuthoringGate.BeginReplacementAsync(scene, CancellationToken.None);
        _ = replacing.IsCompleted.Should().BeFalse();

        var blocked = await fixture.Sut.CreatePrimitiveAsync(context, "Sphere").ConfigureAwait(false);
        _ = blocked.Succeeded.Should().BeFalse();
        _ = scene.RootNodes.Should().ContainSingle();
        native.SetResult();
        _ = (await create.ConfigureAwait(false)).Succeeded.Should().BeTrue();
        using var replacement = await replacing.ConfigureAwait(false);
        _ = replacement.Should().NotBeNull();
        replacement!.Retire();
        replacement.Dispose();
        var previous = scene.Environment.BackgroundColor;
        var late = await fixture.Sut.EditSceneEnvironmentPropertiesAsync(
            context,
            PropertyEdit.Single(SceneDocumentCommandService.SceneEnvironment.BackgroundColor, System.Numerics.Vector3.One),
            "Late background",
            EditSessionToken.OneShot).ConfigureAwait(false);
        _ = late.Succeeded.Should().BeFalse();
        _ = scene.Environment.BackgroundColor.Should().Be(previous);
        _ = context.History.UndoStack.Should().ContainSingle();
    }

    /// <summary>Already-admitted work can finish nested operations while independent input is suspended.</summary>
    /// <returns>The test task.</returns>
    [TestMethod]
    public async Task ReplacementAllowsNestedContinuationOfAnAdmittedOperation()
    {
        var scene = CreateScene();
        var resume = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var nested = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var finish = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        async Task WorkAsync()
        {
            using var outer = SceneAuthoringGate.TryEnter(scene);
            _ = outer.Should().NotBeNull();
            await resume.Task.ConfigureAwait(false);
            using var inner = SceneAuthoringGate.TryEnter(scene);
            _ = inner.Should().NotBeNull();
            nested.SetResult();
            await finish.Task.ConfigureAwait(false);
        }

        var work = WorkAsync();
        var replacing = SceneAuthoringGate.BeginReplacementAsync(scene, CancellationToken.None);
        _ = SceneAuthoringGate.TryEnter(scene).Should().BeNull();
        resume.SetResult();
        await nested.Task.WaitAsync(TimeSpan.FromSeconds(5), this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = replacing.IsCompleted.Should().BeFalse();
        finish.SetResult();
        await work.ConfigureAwait(false);
        using var replacement = await replacing.ConfigureAwait(false);
        _ = replacement.Should().NotBeNull();
    }

    /// <summary>Cancelling a wait preserves the old model and permits subsequent input.</summary>
    /// <returns>The test task.</returns>
    [TestMethod]
    public async Task CancelledReplacementRestoresAdmissionWithoutRetiringTheScene()
    {
        var scene = CreateScene();
        var finish = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        async Task WorkAsync()
        {
            using var operation = SceneAuthoringGate.TryEnter(scene);
            await finish.Task.ConfigureAwait(false);
        }

        var work = WorkAsync();
        using var cancellation = new CancellationTokenSource();
        var replacing = SceneAuthoringGate.BeginReplacementAsync(scene, cancellation.Token);
        await cancellation.CancelAsync().ConfigureAwait(false);
        Func<Task> waiting = () => replacing;
        _ = await waiting.Should().ThrowAsync<OperationCanceledException>().ConfigureAwait(false);
        using var next = SceneAuthoringGate.TryEnter(scene);
        _ = next.Should().NotBeNull();
        finish.SetResult();
        await work.ConfigureAwait(false);
    }

    /// <summary>A disposed lease cannot release or retire a newer replacement attempt.</summary>
    /// <returns>The test task.</returns>
    [TestMethod]
    public async Task StaleReplacementLeaseCannotAffectAnotherAttempt()
    {
        var scene = CreateScene();
        var first = await SceneAuthoringGate.BeginReplacementAsync(scene, CancellationToken.None).ConfigureAwait(false);
        first!.Dispose();
        using var second = await SceneAuthoringGate.BeginReplacementAsync(scene, CancellationToken.None).ConfigureAwait(false);
        first.Dispose();
        _ = SceneAuthoringGate.TryEnter(scene).Should().BeNull();
        var retire = first.Retire;
        _ = retire.Should().Throw<ObjectDisposedException>();
        _ = second.Should().NotBeNull();
    }
}
