// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using DroidNet.Storage.Native;
using Microsoft.Extensions.Logging.Abstractions;
using Moq;
using Oxygen.Editor.Assets;
using Oxygen.Editor.ContentPipeline;
using Oxygen.Editor.ContentPipeline.Mounting;
using Oxygen.Editor.ContentPipeline.Publication;
using Oxygen.Editor.ContentPipeline.Testing;
using Oxygen.Editor.LivePreview.Session;
using Oxygen.Editor.LivePreview.Tests.TestSupport;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Projects.Testing;
using Oxygen.Editor.Runtime.Engine;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.LivePreview.Tests.Session;

/// <summary>
/// Checks the preview session of a project: the engine starts, then the project's content mounts; a failure at either
/// step is the session's state, is published, and is retried from the step that failed, while authoring keeps working.
/// </summary>
[TestClass]
[TestCategory("Preview Session")]
internal sealed partial class PreviewSessionTests
{
    public TestContext TestContext { get; set; }

    [TestMethod]
    public async Task Start_StartsTheEngineThenMountsTheContent()
    {
        using var fixture = new SessionFixture(EngineServiceState.NoEngine);

        var ready = await fixture.Session.StartAsync(this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = ready.Should().BeTrue();
        _ = fixture.Session.State.Should().Be(PreviewSessionState.Ready, fixture.Session.Failure?.Message);
        _ = fixture.Session.Failure.Should().BeNull();
        _ = fixture.Engine.Starts.Should().Be(1);
        _ = fixture.Engine.MountedRoots.Should().ContainSingle();
        fixture.Catalog.Verify(value => value.UsePublicationAsync(It.IsAny<ICookPublicationReadLease>(), It.IsAny<CancellationToken>()), Times.Once);
    }

    [TestMethod]
    public async Task Start_WithARunningEngine_OnlyMountsTheContent()
    {
        using var fixture = new SessionFixture(EngineServiceState.Running);

        _ = await fixture.Session.StartAsync(this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = fixture.Engine.Starts.Should().Be(0);
        _ = fixture.Session.State.Should().Be(PreviewSessionState.Ready, fixture.Session.Failure?.Message);
    }

    [TestMethod]
    public async Task EngineThatCannotStart_LeavesTheEngineUnavailableAndRetryStartsIt()
    {
        using var fixture = new SessionFixture(EngineServiceState.NoEngine);
        fixture.Engine.StartFailure = new InvalidOperationException("No device.");

        var ready = await fixture.Session.StartAsync(this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = ready.Should().BeFalse();
        _ = fixture.Session.State.Should().Be(PreviewSessionState.EngineUnavailable);
        _ = fixture.Session.Failure!.Code.Should().Be(DiagnosticCodes.RuntimePrefix + "START_FAILED");
        _ = fixture.Results.SelectMany(static result => result.Diagnostics).Should().Contain(static diagnostic => diagnostic.Code == DiagnosticCodes.RuntimePrefix + "START_FAILED");
        _ = fixture.Engine.MountedRoots.Should().BeEmpty();

        _ = (await fixture.Session.RetryAsync(this.TestContext.CancellationToken).ConfigureAwait(false)).Should().BeTrue();
        _ = fixture.Session.State.Should().Be(PreviewSessionState.Ready, fixture.Session.Failure?.Message);
        _ = fixture.Session.Failure.Should().BeNull();
        _ = fixture.Engine.MountedRoots.Should().ContainSingle();
    }

    [TestMethod]
    public async Task ContentThatCannotMount_LeavesTheContentUnavailableAndRetryMountsIt()
    {
        using var fixture = new SessionFixture(EngineServiceState.Running);
        fixture.FailCatalog = true;

        _ = await fixture.Session.StartAsync(this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = fixture.Session.State.Should().Be(PreviewSessionState.ContentUnavailable);
        _ = fixture.Session.Failure!.Code.Should().Be(AssetMountDiagnosticCodes.RefreshFailed);
        _ = fixture.Results.SelectMany(static result => result.Diagnostics).Should().Contain(static diagnostic => diagnostic.Code == AssetMountDiagnosticCodes.RefreshFailed);

        fixture.FailCatalog = false;
        _ = (await fixture.Session.RetryAsync(this.TestContext.CancellationToken).ConfigureAwait(false)).Should().BeTrue();
        _ = fixture.Session.State.Should().Be(PreviewSessionState.Ready, fixture.Session.Failure?.Message);
        _ = fixture.Engine.Starts.Should().Be(0, "retrying content does not restart the engine");
    }

    [TestMethod]
    public async Task Disposal_ReleasesTheCookedRootsAndKeepsTheEngineRunning()
    {
        var fixture = new SessionFixture(EngineServiceState.Running);
        _ = await fixture.Session.StartAsync(this.TestContext.CancellationToken).ConfigureAwait(false);

        await fixture.Session.DisposeAsync().ConfigureAwait(false);

        _ = fixture.Engine.MountedRoots[^1].Should().BeEmpty();
        _ = fixture.Engine.State.Should().Be(EngineServiceState.Running);
        fixture.Dispose();
    }

    private sealed partial class SessionFixture : IDisposable
    {
        private readonly DirectoryInfo directory = Directory.CreateTempSubdirectory("Oxygen-PreviewSession-");
        private readonly TestSessionHost sessions;
        private readonly ContentCookCoordinator coordinator;

        public SessionFixture(EngineServiceState state)
        {
            this.Engine = new FakeEngine(state);
            var project = TestProject.Create(new Testably.Abstractions.RealFileSystem(), this.directory.FullName).Context;
            var info = TestProject.InfoOf(project);
            this.sessions = new TestSessionHost();
            var session = this.sessions.Open(project);
            this.coordinator = new ContentCookCoordinator(session, NullLogger<ContentCookCoordinator>.Instance);
            var files = new NativeAtomicFileStore(new Testably.Abstractions.RealFileSystem());
            var manager = new Mock<IProjectManagerService>();
            _ = manager.Setup(value => value.LoadProjectInfoAsync(this.directory.FullName)).ReturnsAsync((IProjectInfo?)info);
            var publication = new CookPublicationService(this.coordinator, session, files, manager.Object, new EngineContentPakFixture());
            _ = this.Catalog.Setup(value => value.UsePublicationAsync(It.IsAny<ICookPublicationReadLease>(), It.IsAny<CancellationToken>()))
                .Returns(() => this.FailCatalog ? Task.FromException(new IOException("The catalog could not read the publication.")) : Task.CompletedTask);
            var publisher = new Mock<IOperationResultPublisher>();
            _ = publisher.Setup(value => value.Publish(It.IsAny<OperationResult>())).Callback<OperationResult>(this.Results.Add);
            this.Session = new PreviewSession(this.Engine, publication, new CookedContentMountService(), this.Catalog.Object, this.coordinator, session, this.sessions, publisher.Object, new OperationStatusReducer());
        }

        public FakeEngine Engine { get; }

        public PreviewSession Session { get; }

        public Mock<IAssetPublicationTarget> Catalog { get; } = new();

        public bool FailCatalog { get; set; }

        public List<OperationResult> Results { get; } = [];

        public void Dispose()
        {
            _ = this.Session.DisposeAsync().AsTask().Wait(TimeSpan.FromSeconds(5));
            this.coordinator.Dispose();
            this.sessions.Dispose();
            try
            {
                this.directory.Delete(recursive: true);
            }
            catch (IOException)
            {
                // A reader the test left open keeps a file; the temporary folder is left behind.
            }
        }
    }
}
