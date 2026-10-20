// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using DroidNet.Storage.Native;
using Microsoft.Extensions.Logging.Abstractions;
using Moq;
using Oxygen.Editor.ContentPipeline;
using Oxygen.Editor.ContentPipeline.Mounting;
using Oxygen.Editor.ContentPipeline.Publication;
using Oxygen.Editor.ContentPipeline.TestSupport;
using Oxygen.Editor.LivePreview.Play;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Projects.Testing;
using Oxygen.Editor.Scenes.Testing;
using Oxygen.Editor.World;
using Oxygen.Managed.Core;
using Oxygen.Managed.Core.Diagnostics;
using Testably.Abstractions.Testing;

namespace Oxygen.Editor.LivePreview.Tests.Play;

/// <summary>
/// Checks what Play does before a player runs: the step a failure names, and what it reads from the scene that played.
/// </summary>
[TestClass]
[TestCategory("Play")]
public sealed partial class PlaySessionTests
{
    public TestContext TestContext { get; set; }

    [TestMethod]
    public async Task MissingPlayer_FailsAtTheLaunchStep()
    {
        using var fixture = new PlayFixture(installedPlayer: false);
        var document = await fixture.Documents.OpenAsync(new Scene { Name = "Test Scene" }.Dehydrate()).ConfigureAwait(false);

        await fixture.Session.PlayAsync(document.Id).ConfigureAwait(false);

        _ = fixture.Session.Status.Activity.Should().Be(PlayActivity.Failed);
        _ = fixture.Session.Status.Failure!.Stage.Should().Be(PlayStage.Launch);
        _ = fixture.Session.Status.Failure.Code.Should().Be(PlayDiagnosticCodes.PlayerMissing);
    }

    [TestMethod]
    public async Task UnsavedDocument_IsRefusedAtTheSaveStep()
    {
        using var fixture = new PlayFixture(installedPlayer: true);
        var document = await fixture.Documents.OpenAsync(new Scene { Name = "Test Scene" }.Dehydrate()).ConfigureAwait(false);
        document.History.AddChange(new DroidNet.TimeMachine.Changes.SimpleAction(static () => { }) { Key = "edit" });

        await fixture.Session.PlayAsync(document.Id).ConfigureAwait(false);

        _ = fixture.Session.Status.Failure!.Stage.Should().Be(PlayStage.Save);
        _ = fixture.Session.Status.Failure.Code.Should().Be(PlayDiagnosticCodes.SaveFailed);
        fixture.Pipeline.Verify(value => value.CookCurrentSceneAsync(It.IsAny<AssetPath>(), It.IsAny<CancellationToken>()), Times.Never);
        _ = fixture.Results.SelectMany(static result => result.Diagnostics).Should().Contain(static diagnostic => diagnostic.Code == PlayDiagnosticCodes.SaveFailed);
    }

    [TestMethod]
    public async Task SceneThatDoesNotCook_FailsAtTheCookStep()
    {
        using var fixture = new PlayFixture(installedPlayer: true);
        var document = await fixture.Documents.OpenAsync(new Scene { Name = "Test Scene" }.Dehydrate()).ConfigureAwait(false);
        _ = fixture.Pipeline.Setup(value => value.CookCurrentSceneAsync(document.File.Asset, It.IsAny<CancellationToken>()))
            .ReturnsAsync(new ContentCookResult(Guid.NewGuid(), CookTargetKind.CurrentScene, OperationStatus.Failed, [], [], Inspection: null, Validation: null));

        await fixture.Session.PlayAsync(document.Id).ConfigureAwait(false);

        _ = fixture.Session.Status.Failure!.Stage.Should().Be(PlayStage.Cook);
        _ = fixture.Session.Status.Failure.Code.Should().Be(PlayDiagnosticCodes.CookFailed);
    }

    [TestMethod]
    public void WithoutAPlayer_NothingComesToTheFrontAndNoProblemNamesANode()
    {
        using var fixture = new PlayFixture(installedPlayer: false);
        var problem = new RuntimeScriptProblem(DiagnosticSeverity.Error, "S0001", "failed", string.Empty) { SlotNode = "Cube" };

        _ = fixture.Session.BringPlayerToFront().Should().BeFalse();
        _ = fixture.Session.NodeOf(problem).Should().BeNull("no scene played");
        _ = fixture.Session.NodeOf(problem with { SlotNode = null }).Should().BeNull();
    }

    private sealed partial class PlayFixture : IDisposable
    {
        private readonly DirectoryInfo directory = Directory.CreateTempSubdirectory("Oxygen-PlaySession-");
        private readonly ContentCookCoordinator coordinator;

        public PlayFixture(bool installedPlayer)
        {
            var projects = new ProjectContextService();
            projects.Activate(this.Documents.Project.Context);
            this.coordinator = new ContentCookCoordinator(projects, NullLogger<ContentCookCoordinator>.Instance);
            var files = new NativeAtomicFileStore(new Testably.Abstractions.RealFileSystem());
            var publication = new CookPublicationService(this.coordinator, projects, files, Mock.Of<IProjectManagerService>(), new EngineContentPakFixture());
            var publisher = new Mock<IOperationResultPublisher>();
            _ = publisher.Setup(value => value.Publish(It.IsAny<OperationResult>())).Callback<OperationResult>(this.Results.Add);
            this.Session = new PlaySession(projects, this.Pipeline.Object, publication, new CookedContentMountService(), new PlaySettings(settings: null), this.Documents.Open, publisher.Object);
            var player = Path.Combine(this.directory.FullName, "Oxygen.Player.exe");
            if (installedPlayer)
            {
                File.WriteAllBytes(player, []);
            }

            this.Session.PlayerExecutable = player;
        }

        public TestSceneDocuments Documents { get; } = new(TestProject.Create(new MockFileSystem(), @"C:\LivePreviewTests"));

        public Mock<IContentPipelineService> Pipeline { get; } = new();

        public PlaySession Session { get; }

        public List<OperationResult> Results { get; } = [];

        public void Dispose()
        {
            this.Session.Dispose();
            this.Documents.Dispose();
            this.coordinator.Dispose();
            this.directory.Delete(recursive: true);
        }
    }
}
