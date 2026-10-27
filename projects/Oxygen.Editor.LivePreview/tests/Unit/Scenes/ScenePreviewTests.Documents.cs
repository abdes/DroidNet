// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Scenes.Documents;
using System.Numerics;
using AwesomeAssertions;
using Oxygen.Editor.ContentPipeline.Inspection;
using Oxygen.Editor.LivePreview.Scenes;
using Oxygen.Editor.LivePreview.Tests.TestSupport;
using Oxygen.Editor.Projects.Testing;
using Oxygen.Editor.Runtime.Engine;
using Oxygen.Editor.Scenes.Testing;
using Oxygen.Editor.World;
using Testably.Abstractions.Testing;

namespace Oxygen.Editor.LivePreview.Tests.Scenes;

/// <summary>
/// Checks that the preview follows the open scene documents: the engine shows the one opened last, its reloads and its
/// close, and every view reads readiness by document id, on the document's thread.
/// </summary>
public sealed partial class ScenePreviewTests
{
    [TestMethod]
    public async Task OpeningASceneDocument_ShowsItAndRaisesReadinessOnItsOwnerThread()
    {
        using var owner = new OwnerContext();
        using var fixture = new PreviewFixture();
        var raisedOn = new TaskCompletionSource<int>(TaskCreationOptions.RunContinuationsAsynchronously);
        fixture.Preview.ReadyChanged += (_, _) => raisedOn.TrySetResult(Environment.CurrentManagedThreadId);

        var document = await owner.RunAsync(() => fixture.Documents.OpenAsync(new Scene { Name = "Test Scene" }.Dehydrate())).ConfigureAwait(false);
        var thread = await raisedOn.Task.WaitAsync(TimeSpan.FromSeconds(5), this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = thread.Should().Be(owner.ThreadId);
        _ = fixture.Preview.IsReady(document.Id).Should().BeTrue();
        _ = fixture.World.Activations.Should().ContainSingle().Which.SceneId.Should().Be(PreviewFixture.SceneOf(document).Id);
    }

    [TestMethod]
    public async Task AViewCreatedLater_ReadsReadinessWithoutAnEvent()
    {
        using var fixture = new PreviewFixture();
        var document = await fixture.Documents.OpenAsync(new Scene { Name = "Test Scene" }.Dehydrate()).ConfigureAwait(false);

        _ = fixture.Preview.IsReady(document.Id).Should().BeTrue();
        _ = fixture.Preview.PendingFieldCount(document.Id).Should().Be(0);
    }

    [TestMethod]
    public async Task OpeningAnotherScene_ShowsItAndClosingItShowsTheSceneStillOpen()
    {
        using var fixture = new PreviewFixture();
        var first = await fixture.Documents.OpenAsync(new Scene { Name = "First" }.Dehydrate()).ConfigureAwait(false);
        var second = await fixture.Documents.OpenAsync(new Scene { Name = "Second" }.Dehydrate()).ConfigureAwait(false);

        _ = fixture.Preview.IsReady(second.Id).Should().BeTrue();
        _ = fixture.Preview.IsReady(first.Id).Should().BeFalse("the engine shows one scene: the document opened last");

        fixture.Documents.Open.Close(second);

        _ = fixture.Preview.IsReady(second.Id).Should().BeFalse();
        _ = fixture.Preview.IsReady(first.Id).Should().BeTrue();
        _ = fixture.World.Activations.Select(static target => target.SceneId)
            .Should().Equal(PreviewFixture.SceneOf(first).Id, PreviewFixture.SceneOf(second).Id, PreviewFixture.SceneOf(first).Id);
    }

    [TestMethod]
    public async Task ClosingTheShownDocument_RetiresItsEngineScene()
    {
        using var fixture = new PreviewFixture();
        var document = await fixture.Documents.OpenAsync(new Scene { Name = "Test Scene" }.Dehydrate()).ConfigureAwait(false);
        var shown = fixture.World.Activations.Single();
        var changes = new List<ScenePreviewEventArgs>();
        fixture.Preview.ReadyChanged += (_, args) => changes.Add(args);

        fixture.Documents.Open.Close(document);

        _ = fixture.World.Invalidations.Should().Contain(shown);
        _ = fixture.Preview.IsReady(document.Id).Should().BeFalse();
        _ = fixture.Preview.PendingFieldCount(document.Id).Should().Be(0);
        _ = changes.Should().ContainSingle().Which.Document.Should().Be(document.Id);
    }

    [TestMethod]
    public async Task ReloadedDocument_ShowsItsNewContent()
    {
        using var fixture = new PreviewFixture();
        var built = new Scene { Name = "Test Scene" };
        var node = new SceneNode(built, "Cube");
        built.RootNodes.Add(node);
        var document = await fixture.Documents.OpenAsync(built.Dehydrate()).ConfigureAwait(false);
        var before = PreviewFixture.SceneOf(document);
        node.Transform.LocalPosition = new Vector3(5, 0, 0);
        fixture.Requests.Clear();

        fixture.Documents.Write(document.File, built.Dehydrate());
        _ = await document.ReloadAsync(this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = PreviewFixture.SceneOf(document).Should().NotBeSameAs(before);
        _ = fixture.Preview.IsReady(document.Id).Should().BeTrue();
        _ = fixture.World.Activations.Should().HaveCount(2);
        _ = fixture.World.Commands.OfType<RuntimeSetLocalTransform>().Should().Contain(command => command.NodeId == node.Id && command.Position.X == 5);
    }

    [TestMethod]
    public async Task EngineThatStops_LeavesNothingReady()
    {
        using var fixture = new PreviewFixture();
        var document = await fixture.Documents.OpenAsync(new Scene { Name = "Test Scene" }.Dehydrate()).ConfigureAwait(false);

        fixture.Engine.ChangeState(EngineServiceState.ShuttingDown);

        _ = fixture.Preview.IsReady(document.Id).Should().BeFalse();
    }

    [TestMethod]
    public async Task DocumentsOpenBeforeThePreview_AreFollowed()
    {
        var engine = new FakeEngine(EngineServiceState.Running);
        await using var lifetime = engine.ConfigureAwait(false);
        using var documents = new TestSceneDocuments(TestProject.Create(new MockFileSystem(), @"C:\LivePreviewTests"));
        var document = await documents.OpenAsync(new Scene { Name = "Test Scene" }.Dehydrate()).ConfigureAwait(false);

        using var preview = new ScenePreview(engine, documents.Open, new DocumentScenes(documents.Open), documents.Session, new SceneStructureStore());

        _ = preview.IsReady(document.Id).Should().BeTrue();
    }

    [TestMethod]
    public async Task SkyLightOfADocumentNotShown_IsNotObserved()
    {
        using var fixture = new PreviewFixture();
        var first = await fixture.Documents.OpenAsync(new Scene { Name = "First" }.Dehydrate()).ConfigureAwait(false);
        _ = await fixture.Documents.OpenAsync(new Scene { Name = "Second" }.Dehydrate()).ConfigureAwait(false);

        var observed = await fixture.Preview.ObserveSkyLightAsync(first.Id, this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = observed.Should().BeNull();
    }
}
