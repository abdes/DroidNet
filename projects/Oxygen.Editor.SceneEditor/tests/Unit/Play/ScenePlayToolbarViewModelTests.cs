// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using DroidNet.Storage;
using Microsoft.Extensions.Logging.Abstractions;
using Moq;
using Oxygen.Editor.ContentPipeline;
using Oxygen.Editor.ContentPipeline.Publication;
using Oxygen.Editor.LivePreview.Play;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Projects.Testing;
using Oxygen.Editor.SceneEditor.Play;
using Oxygen.Editor.Scenes.Testing;
using Oxygen.Editor.World;
using Oxygen.Managed.Core;
using Oxygen.Managed.Core.Diagnostics;
using Testably.Abstractions.Testing;

namespace Oxygen.Editor.SceneEditor.Unit.Tests.Play;

/// <summary>
/// Checks that Play saves the documents the scene uses before it plays: it asks first, and it does not play when they
/// stay unsaved (documents design 5.2.4).
/// </summary>
[TestClass]
[TestCategory("Play")]
internal sealed partial class ScenePlayToolbarViewModelTests
{
    [TestMethod]
    public async Task PlayAsync_WhenTheDocumentsStayUnsaved_DoesNotPlay()
    {
        using var fixture = new ToolbarFixture();
        using var toolbar = await fixture.CreateToolbarAsync(saved: false).ConfigureAwait(false);

        await toolbar.PlayAsync().ConfigureAwait(false);

        _ = fixture.Asked.Should().Be(1);
        _ = fixture.Session.Status.Activity.Should().Be(PlayActivity.Idle);
        fixture.Pipeline.Verify(value => value.CookCurrentSceneAsync(It.IsAny<AssetPath>(), It.IsAny<CancellationToken>()), Times.Never);
    }

    [TestMethod]
    public async Task PlayAsync_WhenTheDocumentsAreSaved_PlaysTheScene()
    {
        using var fixture = new ToolbarFixture();
        using var toolbar = await fixture.CreateToolbarAsync(saved: true).ConfigureAwait(false);

        await toolbar.PlayAsync().ConfigureAwait(false);

        // The scene's cook fails in this fixture, so Play stops at the cook step it reached.
        _ = fixture.Asked.Should().Be(1);
        _ = fixture.Session.Status.Failure!.Code.Should().Be(PlayDiagnosticCodes.CookFailed);
        fixture.Pipeline.Verify(value => value.CookCurrentSceneAsync(fixture.Scene!.File.Asset, It.IsAny<CancellationToken>()), Times.Once);
    }

    [TestMethod]
    public async Task PlayAsync_WhenThereIsNoCamera_AsksNothing()
    {
        using var fixture = new ToolbarFixture();
        using var toolbar = await fixture.CreateToolbarAsync(saved: true, hasCamera: false).ConfigureAwait(false);

        await toolbar.PlayAsync().ConfigureAwait(false);

        _ = fixture.Asked.Should().Be(0);
        _ = fixture.Session.Status.Activity.Should().Be(PlayActivity.Idle);
    }

    private sealed partial class ToolbarFixture : IDisposable
    {
        private readonly DirectoryInfo directory = Directory.CreateTempSubdirectory("Oxygen-PlayToolbar-");
        private readonly TestSession session;
        private readonly ContentCookCoordinator coordinator;

        public ToolbarFixture()
        {
            this.session = new TestSession(this.Documents.Project.Context);
            this.coordinator = new ContentCookCoordinator(this.session, NullLogger<ContentCookCoordinator>.Instance);
            var publication = new CookPublicationService(this.coordinator, this.session, Mock.Of<IAtomicFileStore>(), Mock.Of<IProjectManagerService>(), Mock.Of<IEngineContentPak>());
            var player = Path.Combine(this.directory.FullName, "Oxygen.Player.exe");
            File.WriteAllBytes(player, []);
            _ = this.Pipeline.Setup(value => value.CookCurrentSceneAsync(It.IsAny<AssetPath>(), It.IsAny<CancellationToken>()))
                .ReturnsAsync(new ContentCookResult(Guid.NewGuid(), CookTargetKind.CurrentScene, OperationStatus.Failed, [], [], Inspection: null, Validation: null));
            this.Session = new PlaySession(this.session, this.Pipeline.Object, publication, new PlaySettings(settings: null), this.Documents.Open, Mock.Of<IOperationResultPublisher>(), playerExecutable: player);
        }

        public TestSceneDocuments Documents { get; } = new(TestProject.Create(new MockFileSystem(), @"C:\PlayToolbarTests"));

        public Mock<IContentPipelineService> Pipeline { get; } = new();

        public PlaySession Session { get; }

        public Oxygen.Editor.Documents.Document? Scene { get; private set; }

        public int Asked { get; private set; }

        public async Task<ScenePlayToolbarViewModel> CreateToolbarAsync(bool saved, bool hasCamera = true)
        {
            this.Scene = await this.Documents.OpenAsync(new Scene { Name = "Test Scene" }.Dehydrate()).ConfigureAwait(false);
            var toolbar = new ScenePlayToolbarViewModel(
                this.Session,
                this.Scene.Id,
                () =>
                {
                    this.Asked++;
                    return Task.FromResult(saved);
                },
                placement: static () => null);
            toolbar.UpdateScene(hasCamera);
            return toolbar;
        }

        public void Dispose()
        {
            this.Session.Dispose();
            this.Documents.Dispose();
            this.coordinator.Dispose();
            this.session.Dispose();
            this.directory.Delete(recursive: true);
        }
    }
}
