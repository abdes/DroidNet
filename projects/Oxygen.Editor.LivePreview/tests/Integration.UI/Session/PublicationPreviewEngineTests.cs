// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;
using AwesomeAssertions;
using Oxygen.Editor.ContentPipeline;
using Oxygen.Editor.LivePreview.Scenes;
using Oxygen.Editor.LivePreview.Session;
using Oxygen.Editor.LivePreview.Tests.TestSupport.Native;
using Oxygen.Editor.Projects.Testing;
using Oxygen.Editor.Runtime.Engine;
using Oxygen.Editor.Scenes.Testing;
using Oxygen.Editor.World;
using Oxygen.Managed.Core;
using Oxygen.Managed.Core.Diagnostics;
using Testably.Abstractions.Testing;

namespace Oxygen.Editor.LivePreview.Integration.UI.Tests.Session;

/// <summary>
/// Checks on the real engine how a project's publications reach the preview: a cook swaps the mounted content while
/// the shown scene stays ready, and a cook that stops while its publication preview is being captured, because the
/// project closed or the cook was cancelled, leaves the prior publication selected and mounted.
/// </summary>
[TestClass]
[TestCategory("Integration")]
public sealed partial class PublicationPreviewEngineTests : DroidNet.Tests.VisualUserInterfaceTests
{
    private static readonly Vector4 Red = new(1, 0, 0, 1);
    private static readonly Vector4 Green = new(0, 1, 0, 1);

    public TestContext TestContext { get; set; } = null!;

    [TestMethod]
    public Task PublicationSwapKeepsTheShownSceneReady() => EnqueueAsync(() => this.RunAsync(async (engine, project, token) =>
    {
        var session = project.CreateSession(engine);
        await using var sessionLifetime = session.ConfigureAwait(true);
        _ = (await session.StartAsync(project.Context, token).ConfigureAwait(true)).Should().BeTrue();
        using var documents = new TestSceneDocuments(TestProject.Create(new MockFileSystem(), @"C:\LivePreviewTests"));
        using var preview = new ScenePreview(engine, documents.Open);
        var built = new Scene { Name = "Test Scene" };
        built.RootNodes.Add(new SceneNode(built, "Cube"));
        var document = await documents.OpenAsync(built.Dehydrate()).ConfigureAwait(true);
        await PreviewWaits.UntilReadyAsync(preview, document.Id, token).ConfigureAwait(true);
        var shown = preview.ShownTargetOf(document.Id);

        var cooked = await project.Pipeline.CookAssetAsync(project.WriteMaterial("Swap", Red), token).ConfigureAwait(true);

        _ = cooked.IsPublished.Should().BeTrue();
        _ = cooked.IsMounted.Should().BeTrue();
        _ = preview.IsReady(document.Id).Should().BeTrue();
        _ = preview.ShownTargetOf(document.Id).Should().Be(shown, "a content swap keeps the scene's activation");
        _ = session.State.Should().Be(PreviewSessionState.Ready);
    }));

    [TestMethod]
    public Task ClosingTheProjectWhileAPublicationIsCapturedCancelsTheCookAndKeepsThePriorPublication() => EnqueueAsync(() => this.RunAsync(async (engine, project, token) =>
    {
        var material = project.WriteMaterial("Shared", Red);
        Guid runId;
        FileSystemPath? published;
        var session = project.CreateSession(engine);
        await using (session.ConfigureAwait(true))
        {
            _ = (await session.StartAsync(project.Context, token).ConfigureAwait(true)).Should().BeTrue();
            _ = (await project.Pipeline.CookAssetAsync(material, token).ConfigureAwait(true)).IsMounted.Should().BeTrue();
            published = await project.ReadPublishedContentRootAsync(token).ConfigureAwait(true);
            runId = engine.ContentStatus.RunId;
            using var gate = new CaptureGate(project, engine);
            _ = project.WriteMaterial("Shared", Green);
            var cook = project.Pipeline.CookAssetAsync(material, token);
            await gate.Entered.WaitAsync(token).ConfigureAwait(true);

            project.CloseSession();
            gate.Release();

            _ = (await OutcomeAsync(cook).ConfigureAwait(true)).Should().Be(OperationStatus.Cancelled);
        }

        // Reopening the project mounts the prior publication, and the next cook publishes and mounts the change.
        project.OpenSession();
        _ = (await project.ReadPublishedContentRootAsync(token).ConfigureAwait(true)).Should().Be(published, "the prior publication stays selected");
        var reopened = project.CreateSession(engine);
        await using var reopenedLifetime = reopened.ConfigureAwait(true);
        _ = (await reopened.StartAsync(project.Context, token).ConfigureAwait(true)).Should().BeTrue();
        _ = engine.ContentStatus.RunId.Should().Be(runId, "the engine kept running");
        var retried = await project.Pipeline.CookAssetAsync(material, token).ConfigureAwait(true);
        _ = retried.IsPublished.Should().BeTrue();
        _ = retried.IsMounted.Should().BeTrue();
    }));

    [TestMethod]
    public Task CancellingACookWhileItsPublicationIsCapturedKeepsTheMountedContentUntilRetry() => EnqueueAsync(() => this.RunAsync(async (engine, project, token) =>
    {
        var material = project.WriteMaterial("Shared", Red);
        var session = project.CreateSession(engine);
        await using var sessionLifetime = session.ConfigureAwait(true);
        _ = (await session.StartAsync(project.Context, token).ConfigureAwait(true)).Should().BeTrue();
        _ = (await project.Pipeline.CookAssetAsync(material, token).ConfigureAwait(true)).IsMounted.Should().BeTrue();
        var content = engine.ContentStatus;
        using var gate = new CaptureGate(project, engine);
        _ = project.WriteMaterial("Shared", Green);
        using var cancel = CancellationTokenSource.CreateLinkedTokenSource(token);
        var cook = project.Pipeline.CookAssetAsync(material, cancel.Token);
        await gate.Entered.WaitAsync(token).ConfigureAwait(true);

        await cancel.CancelAsync().ConfigureAwait(true);
        gate.Release();

        _ = (await OutcomeAsync(cook).ConfigureAwait(true)).Should().Be(OperationStatus.Cancelled);
        _ = engine.ContentStatus.Revision.Should().Be(content.Revision, "a cancelled cook mounts nothing");
        var retried = await project.Pipeline.CookAssetAsync(material, token).ConfigureAwait(true);
        _ = retried.IsPublished.Should().BeTrue();
        _ = retried.IsMounted.Should().BeTrue();
        _ = engine.ContentStatus.Revision.Should().BeGreaterThan(content.Revision);
    }));

    // A cancelled cook either reports its cancellation or throws it.
    private static async Task<OperationStatus> OutcomeAsync(Task<ContentCookResult> cook)
    {
        try
        {
            var result = await cook.ConfigureAwait(true);
            return result.IsPublished ? OperationStatus.Succeeded : result.Status;
        }
        catch (OperationCanceledException)
        {
            return OperationStatus.Cancelled;
        }
    }

    private async Task RunAsync(Func<EngineService, PreviewProject, CancellationToken, Task> check)
    {
        using var timeout = CancellationTokenSource.CreateLinkedTokenSource(this.TestContext.CancellationToken);
        timeout.CancelAfter(TimeSpan.FromSeconds(120));
        var engine = await SharedEngine.EnsureRunningAsync(timeout.Token).ConfigureAwait(true);
        try
        {
            using var project = new PreviewProject();
            await check(engine, project, timeout.Token).ConfigureAwait(true);
        }
        finally
        {
            await SharedEngine.ResetAsync().ConfigureAwait(true);
        }
    }

    /// <summary>
    /// Takes over the project's publication preview: a cook's capture waits until released, then gets the preview the
    /// session would give it.
    /// </summary>
    private sealed partial class CaptureGate : IDisposable
    {
        private readonly TaskCompletionSource entered = new(TaskCreationOptions.RunContinuationsAsynchronously);
        private readonly TaskCompletionSource released = new(TaskCreationOptions.RunContinuationsAsynchronously);
        private readonly IDisposable registration;

        public CaptureGate(PreviewProject project, IEngineService engine)
        {
            var owner = OwnerThread.Capture();
            var context = project.Context;
            this.registration = project.Publication.RegisterPreview(context, async () =>
            {
                _ = this.entered.TrySetResult();
                await this.released.Task.ConfigureAwait(false);
                return new PublicationPreview(context, engine, owner, project.Catalog.Object, () => ReferenceEquals(context, project.Sessions.Session?.Context), static () => { });
            });
        }

        public Task Entered => this.entered.Task;

        public void Release() => this.released.TrySetResult();

        public void Dispose()
        {
            this.Release();
            this.registration.Dispose();
        }
    }
}
