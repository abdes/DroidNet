// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Security.Cryptography;
using AwesomeAssertions;
using DroidNet.Storage;
using DroidNet.Storage.Native;
using Microsoft.Extensions.Logging.Abstractions;
using Moq;
using Oxygen.Editor.ContentBrowser.Infrastructure.Assets;
using Oxygen.Editor.ContentPipeline;
using Oxygen.Editor.ContentPipeline.Mounting;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Runtime.Engine;
using Oxygen.Editor.World.Services;
using Oxygen.Managed.Assets.Persistence.LooseCooked.V1;
using Testably.Abstractions;

namespace Oxygen.Editor.World.Tests;

/// <summary>Checks the mount transaction used by the workspace against real project files and reader ownership.</summary>
public sealed partial class InspectorControlTests
{
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
        var accepted = fixture.Projects.ActiveProject;
        Func<Task> apply = () => fixture.Service.ApplyAsync(accepted!, fixture.Candidate, _ => throw new InvalidOperationException("A failed change must not publish context."), this.TestContext.CancellationToken);
        _ = await apply.Should().ThrowAsync<Exception>().ConfigureAwait(true);
        _ = fixture.Projects.ActiveProject.Should().BeSameAs(accepted);
        _ = fixture.Roots.Should().Equal(fixture.ProjectOutput);
        _ = fixture.Paused.Should().BeFalse();
        _ = (await File.ReadAllBytesAsync(fixture.ManifestPath, this.TestContext.CancellationToken).ConfigureAwait(true)).Should().Equal(before);
        File.WriteAllBytes(Path.Combine(fixture.Library, "Material.omat"), [8]);
    });

    /// <summary>A close after the atomic commit does not reactivate the old workspace or resume its preview.</summary>
    /// <returns>The asynchronous post-commit closure regression.</returns>
    [TestMethod]
    public Task ClosingAfterMountSaveDoesNotResurrectTheProject() => EnqueueAsync(async () =>
    {
        using var fixture = new MountTransactionFixture();
        await fixture.InitializeAsync().ConfigureAwait(true);
        fixture.Store.AfterCommit = fixture.Projects.Close;
        var activated = false;
        await fixture.Service.ApplyAsync(fixture.Projects.ActiveProject!, fixture.Candidate, _ => activated = true, this.TestContext.CancellationToken).ConfigureAwait(true);
        _ = fixture.Projects.ActiveProject.Should().BeNull();
        _ = activated.Should().BeFalse();
        _ = fixture.Paused.Should().BeTrue();
        _ = (await File.ReadAllTextAsync(fixture.ManifestPath, this.TestContext.CancellationToken).ConfigureAwait(true)).Should().Contain("Library");
    });

    /// <summary>Successful changes publish context after saving and resume after refreshing the catalog.</summary>
    /// <returns>The asynchronous commit regression.</returns>
    [TestMethod]
    public Task SuccessfulMountChangeCommitsAndResumesInTheNewContext() => EnqueueAsync(async () =>
    {
        using var fixture = new MountTransactionFixture();
        await fixture.InitializeAsync().ConfigureAwait(true);
        var original = fixture.Projects.ActiveProject;
        await fixture.Service.ApplyAsync(
            original!,
            fixture.Candidate,
            next =>
            {
                _ = fixture.Projects.ActiveProject.Should().BeSameAs(original);
                _ = File.ReadAllText(fixture.ManifestPath).Should().Contain("Library");
                _ = fixture.Paused.Should().BeTrue();
            },
            this.TestContext.CancellationToken).ConfigureAwait(true);
        _ = fixture.Projects.ActiveProject!.LocalFolderMounts.Should().ContainSingle();
        _ = fixture.Roots.Should().Equal(fixture.Library, fixture.ProjectOutput);
        _ = fixture.Paused.Should().BeFalse();
    });

    private sealed partial class MountTransactionFixture : IDisposable
    {
        private readonly DirectoryInfo directory = Directory.CreateTempSubdirectory("Oxygen-MountTransaction-");
        private readonly List<IDisposable> nativeReaders = [];
        private readonly Mock<IEngineService> engine = new();
        private readonly ContentCookCoordinator coordinator;
        private readonly ProjectManagerService manager;

        public MountTransactionFixture()
        {
            this.ProjectOutput = this.WriteRoot(".cooked/Content");
            this.Library = this.WriteRoot("Library");
            _ = Directory.CreateDirectory(Path.Combine(this.directory.FullName, "Content"));
            var info = new ProjectInfo("Transaction", Category.Games, this.directory.FullName) { AuthoringMounts = [new("Content", "Content")] };
            this.Projects.Activate(ProjectContext.FromProjectInfo(info));
            this.Candidate = new ProjectInfo(info.Id, info.Name, info.Category, info.Location)
            {
                AuthoringMounts = [.. info.AuthoringMounts], LocalFolderMounts = [new("Library", this.Library)],
                CookedContentOrder = [new(CookedContentSourceKind.LocalFolder, "Library"), new(CookedContentSourceKind.ProjectOutput)],
            };
            this.coordinator = new(this.Projects, NullLogger<ContentCookCoordinator>.Instance);
            var storage = new NativeStorageProvider(new RealFileSystem());
            this.manager = new(storage, atomicFiles: this.Store);
            var api = new Mock<IEngineContentPipelineApi>();
            _ = api.Setup(value => value.ValidateLooseCookedRootAsync(It.IsAny<string>(), It.IsAny<CancellationToken>())).ReturnsAsync((string path, CancellationToken _) => new CookValidationResult(path, Succeeded: true, []));
            var catalog = new Mock<IProjectAssetCatalog>();
            _ = catalog.Setup(value => value.RefreshAsync(It.IsAny<CancellationToken>())).Returns(Task.CompletedTask);
            this.Roots = [this.ProjectOutput];
            this.ConfigureEngine();
            this.Service = new(this.coordinator, this.Projects, this.manager, this.engine.Object, new CookedContentMountService(storage, api.Object), catalog.Object, CreateStatusHosting());
        }

        public MountAtomicStore Store { get; } = new();

        public ProjectContextService Projects { get; } = new();

        public ProjectInfo Candidate { get; }

        public ContentMountChangeService Service { get; }

        public string ProjectOutput { get; }

        public string Library { get; }

        public string ManifestPath => Path.Combine(this.directory.FullName, Oxygen.Editor.Projects.Constants.ProjectFileName);

        public IReadOnlyList<string> Roots { get; private set; }

        public bool Paused { get; private set; }

        public bool FailNative { get; set; }

        public async Task InitializeAsync()
        {
            var context = this.Projects.ActiveProject!;
            var info = new ProjectInfo(context.ProjectId, context.Name, context.Category, context.ProjectRoot) { AuthoringMounts = [.. context.AuthoringMounts] };
            _ = (await this.manager.SaveProjectInfoAsync(info).ConfigureAwait(true)).Should().BeTrue();
        }

        public void Dispose()
        {
            this.coordinator.Dispose();
            this.ReleaseReaders();
            this.directory.Delete(recursive: true);
        }

        private void ConfigureEngine()
        {
            _ = this.engine.SetupGet(value => value.State).Returns(EngineServiceState.Running);
            _ = this.engine.Setup(value => value.SuspendCookedContentAsync()).Returns(() =>
            {
                this.Paused = true;
                this.ReleaseReaders();
                return Task.CompletedTask;
            });
            _ = this.engine.Setup(value => value.ResumeCookedContentAsync()).Returns(() =>
            {
                this.Paused = false;
                return Task.CompletedTask;
            });
            _ = this.engine.Setup(value => value.RefreshProjectCookedRootsAsync(It.IsAny<IReadOnlyList<string>>(), It.IsAny<IDisposable?>(), It.IsAny<bool>()))
                .Returns((IReadOnlyList<string> roots, IDisposable? reader, bool paused) =>
                {
                    this.Roots = roots.ToArray();
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
            LooseCookedIndex.Write(stream, new Document(1, IndexFeatures.HasVirtualPaths, Guid.CreateVersion7(), [new(new AssetKey(1, 2), "Material.omat", "/Content/Material.omat", 1, 1, SHA256.HashData(bytes))], []));
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
            if (this.Fail)
            {
                throw new IOException("Project save failed.");
            }

            var result = await this.inner.WriteAsync(path, content, expected, cancellationToken).ConfigureAwait(false);
            this.AfterCommit?.Invoke();
            return result;
        }
    }
}
