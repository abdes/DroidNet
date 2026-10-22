// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Security.Cryptography;
using AwesomeAssertions;
using DroidNet.Storage;
using DroidNet.Storage.Native;
using Microsoft.Extensions.Logging.Abstractions;
using Moq;
using Oxygen.Editor.Assets;
using Oxygen.Editor.ContentPipeline;
using Oxygen.Editor.ContentPipeline.Mounting;
using Oxygen.Editor.ContentPipeline.Publication;
using Oxygen.Editor.ContentPipeline.Testing;
using Oxygen.Editor.LivePreview.Session;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Projects.Testing;
using Oxygen.Editor.Runtime.Engine;
using Oxygen.Managed.Assets.Persistence;
using Oxygen.Managed.Assets.Persistence.LooseCooked;
using Oxygen.Managed.Core;
using Oxygen.Managed.Core.Diagnostics;
using Testably.Abstractions;

namespace Oxygen.Editor.LivePreview.Unit.UI.Tests.Session;

[TestClass]
internal sealed partial class MountTransactionTests : DroidNet.Tests.VisualUserInterfaceTests
{
    public TestContext TestContext { get; set; } = null!;

    /// <summary>A native or persistence failure restores the old roots and leaves the accepted project file intact.</summary>
    /// <param name="saveFailure">Whether the failure occurs during save or native replacement.</param>
    /// <returns>The asynchronous rollback regression.</returns>
    [TestMethod]
    [DataRow(false)]
    [DataRow(true)]
    public Task FailedMountChangeRestoresNativeRootsAndProjectFile(bool saveFailure) => EnqueueAsync(async () =>
    {
        using var fixture = new MountTransactionFixture();
        await fixture.InitializeAsync().ConfigureAwait(true);
        var before = await File.ReadAllBytesAsync(fixture.ManifestPath, this.TestContext.CancellationToken).ConfigureAwait(true);
        fixture.Store.Fail = saveFailure;
        fixture.FailNative = !saveFailure;
        var accepted = fixture.ProjectSession.Context;
        Func<Task> apply = () => fixture.Session.ChangeMountsAsync(accepted, fixture.Candidate, this.TestContext.CancellationToken);
        _ = await apply.Should().ThrowAsync<Exception>().ConfigureAwait(true);
        _ = fixture.ProjectSession.Context.Should().BeSameAs(accepted);
        _ = fixture.Activations.Should().BeEmpty("a failed change publishes no context");
        _ = fixture.Roots.Should().Equal(fixture.BaseLibrary);
        _ = fixture.Paused.Should().BeFalse();
        _ = (await File.ReadAllBytesAsync(fixture.ManifestPath, this.TestContext.CancellationToken).ConfigureAwait(true)).Should().Equal(before);
        await File.WriteAllBytesAsync(Path.Combine(fixture.Library, "Material.omat"), [8], this.TestContext.CancellationToken).ConfigureAwait(true);
    });

    /// <summary>Closure during configuration save rolls back the uncommitted change without reactivating the project.</summary>
    /// <returns>The asynchronous post-commit closure regression.</returns>
    [TestMethod]
    public Task ClosingAfterMountSaveDoesNotResurrectTheProject() => EnqueueAsync(async () =>
    {
        using var fixture = new MountTransactionFixture();
        await fixture.InitializeAsync().ConfigureAwait(true);
        var original = await File.ReadAllBytesAsync(fixture.ManifestPath, this.TestContext.CancellationToken).ConfigureAwait(true);
        fixture.Store.AfterCommit = () => _ = fixture.Sessions.CloseAsync();
        Func<Task> apply = () => fixture.Session.ChangeMountsAsync(fixture.ProjectSession.Context, fixture.Candidate, this.TestContext.CancellationToken);
        _ = await apply.Should().ThrowAsync<OperationCanceledException>().ConfigureAwait(true);
        _ = fixture.Sessions.Current.Should().BeNull();
        _ = fixture.Activations.Should().BeEmpty("closing changes no mounts, and the change publishes none");
        _ = fixture.Paused.Should().BeTrue();
        _ = (await File.ReadAllBytesAsync(fixture.ManifestPath, this.TestContext.CancellationToken).ConfigureAwait(true)).Should().Equal(original);
    });

    /// <summary>Completed publication activates saved configuration and refreshes its matching catalog.</summary>
    /// <param name="withDefaultScene">Whether the project has a configured default scene to preserve.</param>
    /// <returns>The asynchronous commit regression.</returns>
    [TestMethod]
    [DataRow(false)]
    [DataRow(true)]
    public Task SuccessfulMountChangeCommitsAndResumesInTheNewContext(bool withDefaultScene) => EnqueueAsync(async () =>
    {
        using var fixture = new MountTransactionFixture(withDefaultScene);
        await fixture.InitializeAsync().ConfigureAwait(true);
        var original = fixture.ProjectSession.Context;
        fixture.OnActivated = activated =>
        {
            _ = activated.Should().NotBeNull();
            _ = File.ReadAllText(fixture.ManifestPath).Should().Contain("Library");
            _ = fixture.Paused.Should().BeFalse();
        };
        _ = (await fixture.Session.ChangeMountsAsync(original, fixture.Candidate, this.TestContext.CancellationToken).ConfigureAwait(true)).Should().BeTrue();
        _ = fixture.Activations.Should().ContainSingle().Which.Should().BeSameAs(fixture.ProjectSession.Context, "the session follows the new mounts");
        _ = fixture.ProjectSession.Context.LocalFolderMounts.Select(static mount => mount.Name).Should().Equal("Library", "Base");
        _ = fixture.ProjectSession.Context.DefaultSceneId.Should().Be(original.DefaultSceneId);
        _ = ProjectInfo.FromJson(await File.ReadAllTextAsync(fixture.ManifestPath, this.TestContext.CancellationToken).ConfigureAwait(true)).DefaultSceneId.Should().Be(original.DefaultSceneId);
        _ = fixture.Roots.Should().Equal(fixture.Library, fixture.BaseLibrary);
        _ = fixture.Paused.Should().BeFalse();
    });

    private sealed partial class MountTransactionFixture : IDisposable
    {
        private readonly DirectoryInfo directory = Directory.CreateTempSubdirectory("Oxygen-MountTransaction-");
        private readonly List<IDisposable> nativeReaders = [];
        private readonly Mock<IEngineService> engine = new();
        private readonly ContentCookCoordinator coordinator;
        private readonly ProjectManagerService manager;
        private readonly CookPublicationService publication;

        public MountTransactionFixture(bool withDefaultScene = false)
        {
            this.BaseLibrary = this.WriteRoot("Base");
            this.Library = this.WriteRoot("Library");
            var baseMount = new LocalFolderMount("Base", FileSystemPath.From(this.BaseLibrary));
            var project = TestProject.Create(new RealFileSystem(), this.directory.FullName).Context with
            {
                Name = "Transaction",
                DefaultSceneId = withDefaultScene ? SceneId.New() : null,
                LocalFolderMounts = [baseMount],
                CookedContentOrder = [new(CookedContentSourceKind.LocalFolder, "Base"), new(CookedContentSourceKind.ProjectOutput)],
            };
            this.Sessions = new TestSessionHost();
            var session = this.Sessions.Open(project);
            this.ProjectSession = session;
            this.Candidate = TestProject.InfoOf(project with
            {
                LocalFolderMounts = [new("Library", FileSystemPath.From(this.Library)), baseMount],
                CookedContentOrder = [new(CookedContentSourceKind.LocalFolder, "Library"), new(CookedContentSourceKind.LocalFolder, "Base"), new(CookedContentSourceKind.ProjectOutput)],
            });
            this.coordinator = new(session, NullLogger<ContentCookCoordinator>.Instance);
            var storage = new NativeStorageProvider(new RealFileSystem());
            this.manager = new(storage, atomicFiles: this.Store);
            this.publication = new(this.coordinator, session, this.Store, this.manager, new EngineContentPakFixture());
            var catalog = new Mock<IAssetPublicationTarget>();
            _ = catalog.Setup(value => value.UsePublicationAsync(It.IsAny<ICookPublicationReadLease>(), It.IsAny<CancellationToken>())).Returns(Task.CompletedTask);
            this.Roots = [this.BaseLibrary];
            this.ConfigureEngine();
            this.Session = new(this.engine.Object, this.publication, new CookedContentMountService(), catalog.Object, this.coordinator, session, this.Sessions, Mock.Of<IOperationResultPublisher>(), new OperationStatusReducer());
        }

        public MountAtomicStore Store { get; } = new();

        public TestSessionHost Sessions { get; }

        /// <summary>Gets the project's session, whose context a mount change replaces.</summary>
        public TestSession ProjectSession { get; }

        public ProjectInfo Candidate { get; }

        public PreviewSession Session { get; }

        /// <summary>Gets the contexts the session changed to after the preview started, in order.</summary>
        public List<ProjectContext> Activations { get; } = [];

        /// <summary>Gets or sets what runs when the session changes its context, before the change completes.</summary>
        public Action<ProjectContext>? OnActivated { get; set; }

        /// <summary>Gets the library the project mounts before the change; the startup publication holds it.</summary>
        public string BaseLibrary { get; }

        /// <summary>Gets the library the change mounts ahead of <see cref="BaseLibrary"/>.</summary>
        public string Library { get; }

        public string ManifestPath => Path.Combine(this.directory.FullName, Oxygen.Editor.Projects.Constants.ProjectFileName);

        public IReadOnlyList<string> Roots { get; private set; }

        public bool Paused { get; private set; }

        public bool FailNative { get; set; }

        public async Task InitializeAsync()
        {
            var context = this.ProjectSession.Context;
            _ = (await this.manager.SaveProjectInfoAsync(TestProject.InfoOf(context)).ConfigureAwait(true)).Should().BeTrue();

            // Starting publishes the mounted library, the publication the change starts from.
            _ = (await this.Session.StartAsync().ConfigureAwait(true)).Should().BeTrue();

            // Only the changes after the preview started count.
            this.ProjectSession.ContextChanged += this.OnContextChanged;
        }

        public void Dispose()
        {
            this.ProjectSession.ContextChanged -= this.OnContextChanged;
            _ = this.Session.DisposeAsync().AsTask().Wait(TimeSpan.FromSeconds(5));
            this.coordinator.Dispose();
            this.Sessions.Dispose();
            this.ReleaseReaders();
            this.directory.Delete(recursive: true);
        }

        private void OnContextChanged(object? sender, EventArgs args)
        {
            this.Activations.Add(this.ProjectSession.Context);
            this.OnActivated?.Invoke(this.ProjectSession.Context);
        }

        private void ConfigureEngine()
        {
            _ = this.engine.SetupGet(value => value.State).Returns(EngineServiceState.Running);

            // The engine service always gives world commands; the session follows the run they belong to.
            _ = this.engine.SetupGet(value => value.WorldCommands).Returns(Mock.Of<IRuntimeWorldCommands>(world => world.RunId == Guid.NewGuid()));
            _ = this.engine.Setup(value => value.SuspendCookedContentAsync()).Returns(() =>
            {
                this.Paused = true;
                return Task.CompletedTask;
            });
            _ = this.engine.Setup(value => value.ResumeCookedContentAsync()).Returns(() =>
            {
                this.Paused = false;
                return Task.CompletedTask;
            });
            _ = this.engine.Setup(value => value.RefreshProjectCookedRootsAsync(It.IsAny<IReadOnlyList<RuntimeCookedRoot>>(), It.IsAny<IDisposable?>(), It.IsAny<bool>())).Returns((IReadOnlyList<RuntimeCookedRoot> roots, IDisposable? reader, bool paused) =>
            {
                this.Roots = [.. roots.Select(static root => root.Path)];
                if (reader is not null)
                {
                    this.nativeReaders.Add(reader);
                }

                if (this.FailNative)
                {
                    this.FailNative = false;
                    throw new IOException("Native replacement failed after taking ownership.");
                }

                foreach (var prior in this.nativeReaders.Where(value => !ReferenceEquals(value, reader)).ToArray())
                {
                    prior.Dispose();
                    _ = this.nativeReaders.Remove(prior);
                }

                this.Paused = paused;
                return Task.CompletedTask;
            });
        }

        private void ReleaseReaders()
        {
            foreach (var reader in this.nativeReaders)
            {
                reader.Dispose();
            }

            this.nativeReaders.Clear();
        }

        private string WriteRoot(string relative)
        {
            var root = Path.GetFullPath(Path.Combine(this.directory.FullName, relative));
            _ = Directory.CreateDirectory(root);
            byte[] bytes = [1];
            File.WriteAllBytes(Path.Combine(root, "Material.omat"), bytes);
            using var stream = File.Create(Path.Combine(root, "container.index.bin"));
            Oxygen.Managed.Assets.Testing.LooseCookedIndexFixture.Write(stream, new Document(1, IndexFeatures.HasVirtualPaths, Guid.CreateVersion7(), [new(new AssetKey(1, 2), "Material.omat", "/Content/Material.omat", 1, 1, SHA256.HashData(bytes))], []));
            return root;
        }
    }

    private sealed class MountAtomicStore : IAtomicFileStore
    {
        private readonly NativeAtomicFileStore inner = new(new RealFileSystem());

        public bool Fail { get; set; }

        public Action? AfterCommit { get; set; }

        public Task<FileSnapshot> ReadAsync(string path, CancellationToken cancellationToken = default) => this.inner.ReadAsync(path, cancellationToken);

        public async Task<FileVersion> WriteAsync(string path, ReadOnlyMemory<byte> content, FileVersion expected, CancellationToken cancellationToken = default)
        {
            var projectSave = string.Equals(Path.GetFileName(path), Oxygen.Editor.Projects.Constants.ProjectFileName, StringComparison.OrdinalIgnoreCase);
            if (this.Fail && projectSave)
            {
                throw new IOException("Project save failed.");
            }

            var result = await this.inner.WriteAsync(path, content, expected, cancellationToken).ConfigureAwait(false);
            if (projectSave)
            {
                this.AfterCommit?.Invoke();
            }

            return result;
        }
    }
}
