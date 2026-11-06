// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Microsoft.Extensions.Logging.Abstractions;
using Moq;
using Oxygen.Editor.ContentPipeline.Mounting;
using Oxygen.Editor.LivePreview.Session;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Projects.Mounts;
using Oxygen.Editor.Projects.Testing;
using Oxygen.Managed.Assets;
using Oxygen.Managed.Assets.Persistence;
using Oxygen.Managed.Assets.Persistence.Pak;
using Oxygen.Managed.Assets.Persistence.Publication;
using Oxygen.Managed.Assets.Testing;
using Oxygen.Managed.Core;
using Oxygen.Managed.Core.Diagnostics;
using Testably.Abstractions.Testing;

namespace Oxygen.Editor.LivePreview.Tests.Session;

/// <summary>
/// A <c>.mounts.json</c> change during a session is a mount change: the watcher resolves the table again when the
/// project's own file or one above it changes, and applies a table that differs, with the project's declarations as
/// they are.
/// </summary>
[TestClass]
[TestCategory("Mounts")]
public sealed class MountsFileWatcherTests : IDisposable
{
    private const string Parent = @"C:\Studio";
    private const string Root = @"C:\Studio\Games\Demo";
    private const string Shared = @"D:\Shared";
    private const string Cooked = @"C:\Studio\Props\.cooked";

    private readonly MockFileSystem fileSystem = new();
    private readonly Mock<IContentMountChanges> changes = new();
    private readonly Mock<IOperationResultPublisher> results = new();
    private readonly TaskCompletionSource<(ProjectContext Expected, ProjectInfo Candidate, MountBindings Bindings)> applied = new(TaskCreationOptions.RunContinuationsAsynchronously);
    private readonly TestSession session;

    public MountsFileWatcherTests()
    {
        _ = this.fileSystem.Directory.CreateDirectory(Shared);
        var info = new ProjectInfo(Guid.NewGuid(), "Demo", Category.Games, Root)
        {
            Mounts = [new ProjectMount("Content", MountKind.Source, "Content"), new ProjectMount("Shared")],
        };
        _ = this.fileSystem.Directory.CreateDirectory(Path.Combine(Root, "Content"));
        this.session = new(TestProject.Open(info, this.fileSystem).Context);
        _ = this.changes.Setup(value => value.ChangeMountsAsync(It.IsAny<ProjectContext>(), It.IsAny<ProjectInfo>(), It.IsAny<MountBindings>(), It.IsAny<CancellationToken>()))
            .Callback<ProjectContext, ProjectInfo, MountBindings, CancellationToken>((expected, candidate, bindings, _) => this.applied.TrySetResult((expected, candidate, bindings)))
            .ReturnsAsync(true);
    }

    public TestContext TestContext { get; set; } = null!;

    [TestMethod]
    public async Task AnUnchangedTable_IsNotApplied()
    {
        using var watcher = this.CreateWatcher();

        var changed = await watcher.ApplyAsync(this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = changed.Should().BeFalse();
        this.changes.VerifyNoOtherCalls();
    }

    [TestMethod]
    public async Task ABindingInTheProjectsOwnFile_IsAppliedWithTheDeclarationsAsTheyAre()
    {
        using var watcher = this.CreateWatcher();
        this.fileSystem.File.WriteAllText(Path.Combine(Root, ProjectMounts.FileName), $$$"""{"schema_version":{{{ProjectMounts.SchemaVersion}}},"mounts":{"Shared":"D:/Shared"}}""");

        var changed = await watcher.ApplyAsync(this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = changed.Should().BeTrue();
        var (expected, candidate, bindings) = await this.applied.Task.ConfigureAwait(false);
        _ = expected.Should().BeSameAs(this.session.Context);
        _ = candidate.Mounts.Should().Equal(this.session.Context.DeclaredMounts);
        _ = bindings.IsEmpty.Should().BeTrue("the file is already written");
    }

    [TestMethod]
    [DataRow(Root, DisplayName = "The project's own file")]
    [DataRow(Parent, DisplayName = "A file above the project")]
    public async Task WritingAMountsFile_AppliesTheNewTable(string folder)
    {
        using var watcher = this.CreateWatcher();

        this.fileSystem.File.WriteAllText(Path.Combine(folder, ProjectMounts.FileName), $$$"""{"schema_version":{{{ProjectMounts.SchemaVersion}}},"mounts":{"Shared":"D:/Shared"}}""");

        var (_, candidate, _) = await this.applied.Task.WaitAsync(TimeSpan.FromSeconds(10), this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = MountChange.CandidateContext(this.session.Context, candidate).Mounts.Single(static mount => mount.Name == "Shared").Location
            .Should().Be(FileSystemPath.From(Shared));
    }

    /// <summary>A new cook of a library bound by its .cooked folder applies the generation it selects.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task ALibrarysNewHead_AppliesItsGeneration()
    {
        using var library = this.OpenWithLibrary();
        using var watcher = this.CreateWatcher(library);
        var next = this.Publish("/Props/Crate.omat", "/Props/Barrel.omat");

        var (expected, candidate, bindings) = await this.applied.Task.WaitAsync(TimeSpan.FromSeconds(10), this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = expected.Should().BeSameAs(library.Context);
        _ = bindings.IsEmpty.Should().BeTrue();
        var mount = MountChange.CandidateContext(library.Context, candidate).Mounts.Single(static mount => mount.Name == "Props");
        _ = mount.Container.Should().Be(MountContainer.Generation);
        _ = mount.Location.Should().Be(FileSystemPath.From(Path.Combine(Cooked, "generations", next.ToString("N"))));
    }

    /// <summary>A new generation that removes what a pak above it uses is refused, and the finding is published.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task AGenerationThatBreaksAPakAbove_IsRefused()
    {
        WritePak(this.fileSystem, @"C:\Paks\Kit.pak", "Kit", "/Kit/Box.omat", "/Props/Crate.omat");
        using var library = this.OpenWithLibrary(new ProjectMount("Kit", MountKind.Cooked, "../../../Paks/Kit.pak"));
        using var watcher = this.CreateWatcher(library);
        var kit = library.Context.Mounts.Single(static mount => mount.Name == "Kit");
        _ = kit.IsBound.Should().BeTrue(string.Join(" | ", kit.Findings.Select(static finding => finding.Message)) + kit.State);
        _ = this.Publish("/Props/Barrel.omat");

        var changed = await watcher.ApplyAsync(this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = changed.Should().BeFalse();
        this.changes.VerifyNoOtherCalls();
        this.results.Verify(value => value.Publish(It.Is<OperationResult>(result => result.Diagnostics.Any(static diagnostic => diagnostic.Code == MountCodes.CookedDependencyMissing))), Times.Once);
    }

    /// <summary>A loose library that lacks what it needs mounts, with the finding.</summary>
    [TestMethod]
    public void ALibraryThatLacksItsNeed_StaysBoundWithTheFinding()
    {
        var info = new ProjectInfo(Guid.NewGuid(), "Needs", Category.Games, Root) { Mounts = [new ProjectMount("Content", MountKind.Source, "Content"), new ProjectMount("Props", MountKind.Cooked)] };
        _ = this.PublishRequiring("/Vehicles/Paint.omat", "/Props/Crate.omat");
        this.Bind();

        var mount = TestProject.Open(info, this.fileSystem).Context.Mounts.Single(static mount => mount.Name == "Props");

        _ = mount.State.Should().Be(MountState.Bound, string.Join(" | ", mount.Findings.Select(static finding => finding.Message)));
        _ = mount.Findings.Should().ContainSingle().Which.Code.Should().Be(MountCodes.CookedDependencyMissing);
    }

    public void Dispose() => this.session.Dispose();

    private static void WritePak(MockFileSystem files, string path, string library, string asset, string needs)
    {
        _ = files.Directory.CreateDirectory(Path.GetDirectoryName(path)!);
        using var stream = new MemoryStream();
        PakFixture.Write(stream, new PakDocument(1, Guid.CreateVersion7(), new string('0', 64), [new PakAssetEntry(AssetKey.FromVirtualPath(asset), (byte)AssetType.Material, asset, 0, 0)])
        {
            Library = library,
            Dependencies = [new LibraryDependency("Props", [new LibraryRequirement(IsTexture: false, AssetKey.FromVirtualPath(needs), (byte)AssetType.Material)])],
        });
        files.File.WriteAllBytes(path, stream.ToArray());
    }

    // A session whose Props mount follows a library project's .cooked folder; other mounts are declared above it.
    private TestSession OpenWithLibrary(params ProjectMount[] above)
    {
        _ = this.Publish("/Props/Crate.omat");
        this.Bind();
        var info = new ProjectInfo(Guid.NewGuid(), "Follows", Category.Games, Root)
        {
            Mounts = [new ProjectMount("Content", MountKind.Source, "Content"), new ProjectMount("Props", MountKind.Cooked), .. above],
        };
        return new(TestProject.Open(info, this.fileSystem).Context);
    }

    private void Bind()
        => this.fileSystem.File.WriteAllText(Path.Combine(Root, ProjectMounts.FileName), $$$"""{"schema_version":{{{ProjectMounts.SchemaVersion}}},"mounts":{"Props":"C:/Studio/Props/.cooked"}}""");

    // Publishes a new generation of the Props root, as a library project's cook does; the head moves last.
    private Guid Publish(params string[] assets) => this.PublishRequiring(null, assets);

    private Guid PublishRequiring(string? requires, params string[] assets)
    {
        var generation = Guid.CreateVersion7();
        var folder = Path.Combine(Cooked, "generations", generation.ToString("N"));
        _ = this.fileSystem.Directory.CreateDirectory(folder);
        using (var index = new MemoryStream())
        {
            LooseCookedIndexFixture.Write(index, new Oxygen.Managed.Assets.Persistence.LooseCooked.Document(
                1,
                default,
                Guid.CreateVersion7(),
                [.. assets.Select(static path => new Oxygen.Managed.Assets.Persistence.LooseCooked.AssetEntry(AssetKey.FromVirtualPath(path), CookedLibraryFixture.DescriptorOf(path), path, (byte)AssetType.Material, 4, System.Security.Cryptography.SHA256.HashData(System.Text.Encoding.UTF8.GetBytes(path))))],
                [])
            {
                Requirements = requires is null ? [] : [new Oxygen.Managed.Assets.Persistence.LooseCooked.AssetRequirement(0, "Vehicles", IsTexture: false, AssetKey.FromVirtualPath(requires), (byte)AssetType.Material)],
            });
            this.fileSystem.File.WriteAllBytes(Path.Combine(folder, "container.index.bin"), index.ToArray());
        }

        var files = CookedLibraryFixture.Publication(("Props", generation));
        foreach (var (path, bytes) in files.OrderBy(static file => file.Key == CookPublicationFormat.HeadFileName))
        {
            var target = Path.Combine(Cooked, path.Replace('/', '\\'));
            _ = this.fileSystem.Directory.CreateDirectory(Path.GetDirectoryName(target)!);
            this.fileSystem.File.WriteAllBytes(target, bytes);
        }

        return generation;
    }

    private MountsFileWatcher CreateWatcher(TestSession? session = null) => new(session ?? this.session, this.changes.Object, this.results.Object, new OperationStatusReducer(), NullLogger<MountsFileWatcher>.Instance);
}
