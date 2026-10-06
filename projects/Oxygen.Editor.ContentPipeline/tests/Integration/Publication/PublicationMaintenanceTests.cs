// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using DroidNet.Storage;
using DroidNet.Storage.Native;
using Moq;
using Oxygen.Editor.ContentPipeline.Publication;
using Oxygen.Editor.ContentPipeline.TestSupport;
using Oxygen.Editor.Projects;
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
        var service = MaintenanceService(files);
        CookPublicationReadLease old;
        string currentPath;
        using (var gate = await CookOutputLease.AcquireWriteAsync(project.Root, this.TestContext.CancellationToken).ConfigureAwait(false))
        {
            _ = await WritePublicationFixtureAsync(context, files, FileVersion.Missing, this.TestContext.CancellationToken).ConfigureAwait(false);
            old = await CookPublicationReadLease.OpenUnderGateAsync(context, files, gate, this.TestContext.CancellationToken).ConfigureAwait(false);
            currentPath = await WritePublicationFixtureAsync(context, files, old.Head.Version, this.TestContext.CancellationToken).ConfigureAwait(false);
        }

        using var lifetime = old;
        var oldPath = old.FindProjectRoot("Content")!;
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
        var service = MaintenanceService(files);
        string oldPath;
        using (var gate = await CookOutputLease.AcquireWriteAsync(project.Root, this.TestContext.CancellationToken).ConfigureAwait(false))
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
        nativeReader.Dispose();
        _ = (await service.MaintainAsync(context, this.TestContext.CancellationToken).ConfigureAwait(false)).Should().BeEmpty();
        _ = Directory.Exists(oldPath).Should().Be(removeMarker, "nonempty markerless published roots cannot be claimed safely");
    }

    [TestMethod]
    public async Task RetryInputPinProtectsOnlyOperationScratch()
    {
        using var project = new ProjectDirectory();
        var context = PublicationContext(project.Root);
        var files = new NativeAtomicFileStore(new RealFileSystem());
        var service = MaintenanceService(files);
        var operation = new ContentCookOperation(Guid.NewGuid(), context, 1);
        using var owner = CookOutputLease.AcquireOperation(project.Root, operation.OperationId);
        using var baseline = await service.AcquireReadAsync(context, this.TestContext.CancellationToken).ConfigureAwait(false);
        var transaction = await CookPublicationTransaction.ReserveAsync(operation, baseline, [], files,
            new ProjectManagerService(new NativeStorageProvider(new RealFileSystem())), this.TestContext.CancellationToken).ConfigureAwait(false);
        await transaction.AbandonBuildAsync().ConfigureAwait(false);
        var directory = Path.Combine(project.Root, ".build", "cook", operation.OperationId.ToString("N"));
        var input = Path.Combine(directory, "replacement-input.gltf");
        File.WriteAllText(input, "retry bytes");
        using var retry = CookOutputLease.TryAcquireRetryInput(project.Root, operation.OperationId);
        _ = retry.Should().NotBeNull();
        owner.Dispose();
        _ = (await service.MaintainAsync(context, this.TestContext.CancellationToken).ConfigureAwait(false)).Should().BeEmpty();
        _ = File.ReadAllText(input).Should().Be("retry bytes");
        retry!.Dispose();
        _ = (await service.MaintainAsync(context, this.TestContext.CancellationToken).ConfigureAwait(false)).Should().BeEmpty();
        _ = Directory.Exists(directory).Should().BeFalse();
    }

    [TestMethod]
    public async Task DrainedScratchWithoutAPublicationJournalIsReclaimed()
    {
        using var project = new ProjectDirectory();
        var context = PublicationContext(project.Root);
        var files = new NativeAtomicFileStore(new RealFileSystem());
        var service = MaintenanceService(files);
        var id = Guid.NewGuid();
        using var operation = CookOutputLease.AcquireOperation(project.Root, id);
        var directory = Path.Combine(project.Root, ".build", "cook", id.ToString("N"));
        var inputs = Directory.CreateDirectory(Path.Combine(directory, "inputs"));
        var captured = Path.Combine(inputs.FullName, "source.gltf");
        File.WriteAllText(captured, "captured input");
        _ = (await service.MaintainAsync(context, this.TestContext.CancellationToken).ConfigureAwait(false)).Should().BeEmpty();
        _ = File.Exists(captured).Should().BeTrue();
        operation.Dispose();
        _ = (await service.MaintainAsync(context, this.TestContext.CancellationToken).ConfigureAwait(false)).Should().BeEmpty();
        _ = Directory.Exists(directory).Should().BeFalse();
    }

    private static CookPublicationService MaintenanceService(IAtomicFileStore files)
        => new(Mock.Of<IContentCookCoordinator>(), Mock.Of<IProjectContextService>(), files,
            new ProjectManagerService(new NativeStorageProvider(new RealFileSystem()), atomicFiles: files));
}
