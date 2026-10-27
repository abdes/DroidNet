// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using DroidNet.Storage;
using DroidNet.Storage.Native;
using Moq;
using Oxygen.Editor.ContentPipeline.Publication;
using Oxygen.Editor.ContentPipeline.Testing;
using Oxygen.Editor.ContentPipeline.TestSupport;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Projects.Testing;
using Oxygen.Managed.Core;
using Testably.Abstractions;
using static Oxygen.Editor.ContentPipeline.TestSupport.GenerationScenario;

namespace Oxygen.Editor.ContentPipeline.Integration.Tests.Publication;

[TestClass]
public sealed class PublicationMaintenanceTests
{
    public TestContext TestContext { get; set; } = null!;

    [TestMethod]
    public async Task MaintenanceReclaimsOldPublicationOnlyAfterItsLastReaderCloses()
    {
        using var project = new ProjectDirectory();
        var context = PublicationContext(project.Root);
        var files = new NativeAtomicFileStore(new RealFileSystem());
        using var session = new TestSession(context);
        var service = MaintenanceService(files, session);
        CookPublicationReadLease old;
        string currentPath;
        using (var gate = await CookOutputLease.AcquireWriteAsync(FileSystemPath.From(project.Root), this.TestContext.CancellationToken).ConfigureAwait(false))
        {
            _ = await WritePublicationFixtureAsync(context, files, FileVersion.Missing, this.TestContext.CancellationToken).ConfigureAwait(false);
            old = await CookPublicationReadLease.OpenUnderGateAsync(context, files, gate, this.TestContext.CancellationToken).ConfigureAwait(false);
            currentPath = await WritePublicationFixtureAsync(context, files, old.Head.Version, this.TestContext.CancellationToken).ConfigureAwait(false);
        }

        using var lifetime = old;
        var oldPath = old.FindProjectRoot("Content")!.FullPath;
        var oldDocument = CookPublicationPaths.Document(project.Root, old.PublicationId!.Value);
        _ = (await service.MaintainAsync(context, this.TestContext.CancellationToken).ConfigureAwait(false)).Should().BeEmpty();
        _ = Directory.Exists(oldPath).Should().BeTrue();
        _ = File.Exists(oldDocument).Should().BeTrue();
        old.Dispose();
        _ = (await service.MaintainAsync(context, this.TestContext.CancellationToken).ConfigureAwait(false)).Should().BeEmpty();
        _ = Directory.Exists(oldPath).Should().BeFalse();
        _ = File.Exists(oldDocument).Should().BeFalse();
        _ = Directory.Exists(currentPath).Should().BeTrue();
    }

    [TestMethod]
    [DataRow(false)]
    [DataRow(true)]
    public async Task StandaloneGenerationReadersProtectPayloadsAfterDocumentReclamation(bool removeMarker)
    {
        using var project = new ProjectDirectory();
        var context = PublicationContext(project.Root);
        var files = new NativeAtomicFileStore(new RealFileSystem());
        using var session = new TestSession(context);
        var service = MaintenanceService(files, session);
        string oldPath;
        using (var gate = await CookOutputLease.AcquireWriteAsync(FileSystemPath.From(project.Root), this.TestContext.CancellationToken).ConfigureAwait(false))
        {
            oldPath = await WritePublicationFixtureAsync(context, files, FileVersion.Missing, this.TestContext.CancellationToken).ConfigureAwait(false);
            using var old = await CookPublicationReadLease.OpenUnderGateAsync(context, files, gate, this.TestContext.CancellationToken).ConfigureAwait(false);
            _ = await WritePublicationFixtureAsync(context, files, old.Head.Version, this.TestContext.CancellationToken).ConfigureAwait(false);
        }

        var marker = Path.Combine(oldPath, CookedGeneration.MarkerFileName);
        using var nativeReader = WindowsCookFile.OpenGenerationReader(marker);
        if (removeMarker)
        {
            File.Delete(marker);
        }

        _ = (await service.MaintainAsync(context, this.TestContext.CancellationToken).ConfigureAwait(false)).Should().BeEmpty();
        _ = File.Exists(Path.Combine(oldPath, "container.index.bin")).Should().BeTrue();
        await nativeReader.DisposeAsync().ConfigureAwait(false);
        _ = (await service.MaintainAsync(context, this.TestContext.CancellationToken).ConfigureAwait(false)).Should().BeEmpty();
        _ = Directory.Exists(oldPath).Should().Be(removeMarker, "nonempty markerless published roots cannot be claimed safely");
    }

    [TestMethod]
    public async Task RetryInputPinProtectsOnlyOperationScratch()
    {
        using var project = new ProjectDirectory();
        var context = PublicationContext(project.Root);
        var files = new NativeAtomicFileStore(new RealFileSystem());
        using var session = new TestSession(context);
        var service = MaintenanceService(files, session);
        var operation = new ContentCookOperation(Guid.NewGuid(), context, 1);
        using var owner = CookOutputLease.AcquireOperation(FileSystemPath.From(project.Root), operation.OperationId);
        using var baseline = await service.AcquireReadAsync(context, this.TestContext.CancellationToken).ConfigureAwait(false);
        var transaction = await CookPublicationTransaction.ReserveAsync(
            operation,
            baseline,
            [],
            files,
            new ProjectManagerService(new NativeStorageProvider(new RealFileSystem())),
            this.TestContext.CancellationToken).ConfigureAwait(false);
        await transaction.AbandonBuildAsync().ConfigureAwait(false);
        var directory = Path.Combine(project.Root, ".build", "cook", operation.OperationId.ToString("N"));
        var input = Path.Combine(directory, "replacement-input.gltf");
        await File.WriteAllTextAsync(input, "retry bytes", this.TestContext.CancellationToken).ConfigureAwait(false);
        using var retry = CookOutputLease.TryAcquireRetryInput(FileSystemPath.From(project.Root), operation.OperationId);
        _ = retry.Should().NotBeNull();
        await owner.DisposeAsync().ConfigureAwait(false);
        _ = (await service.MaintainAsync(context, this.TestContext.CancellationToken).ConfigureAwait(false)).Should().BeEmpty();
        _ = (await File.ReadAllTextAsync(input, this.TestContext.CancellationToken).ConfigureAwait(false)).Should().Be("retry bytes");
        await retry!.DisposeAsync().ConfigureAwait(false);
        _ = (await service.MaintainAsync(context, this.TestContext.CancellationToken).ConfigureAwait(false)).Should().BeEmpty();
        _ = Directory.Exists(directory).Should().BeFalse();
    }

    [TestMethod]
    public async Task DrainedScratchWithoutAPublicationJournalIsReclaimed()
    {
        using var project = new ProjectDirectory();
        var context = PublicationContext(project.Root);
        var files = new NativeAtomicFileStore(new RealFileSystem());
        using var session = new TestSession(context);
        var service = MaintenanceService(files, session);
        var id = Guid.NewGuid();
        using var operation = CookOutputLease.AcquireOperation(FileSystemPath.From(project.Root), id);
        var directory = Path.Combine(project.Root, ".build", "cook", id.ToString("N"));
        var inputs = Directory.CreateDirectory(Path.Combine(directory, "inputs"));
        var captured = Path.Combine(inputs.FullName, "source.gltf");
        await File.WriteAllTextAsync(captured, "captured input", this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = (await service.MaintainAsync(context, this.TestContext.CancellationToken).ConfigureAwait(false)).Should().BeEmpty();
        _ = File.Exists(captured).Should().BeTrue();
        await operation.DisposeAsync().ConfigureAwait(false);
        _ = (await service.MaintainAsync(context, this.TestContext.CancellationToken).ConfigureAwait(false)).Should().BeEmpty();
        _ = Directory.Exists(directory).Should().BeFalse();
    }

    private static CookPublicationService MaintenanceService(IAtomicFileStore files, TestSession session)
        => new(
            Mock.Of<IContentCookCoordinator>(),
            session,
            files,
            new ProjectManagerService(new NativeStorageProvider(new RealFileSystem()), atomicFiles: files),
            new EngineContentPakFixture());
}
