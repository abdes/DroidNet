// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using System.Text.Json.Nodes;
using AwesomeAssertions;
using DroidNet.Config;
using DroidNet.Storage;
using DroidNet.Storage.Native;
using Microsoft.Extensions.Logging.Abstractions;
using Moq;
using Oxygen.Editor.ContentPipeline.Discovery;
using Oxygen.Managed.Core.Compatibility;

namespace Oxygen.Editor.ContentPipeline.Tests;

/// <summary>Verifies native catalog caching, offline authoring and SDK restoration.</summary>
[TestClass]
[SuppressMessage("Maintainability", "CA1515:Consider making public types internal", Justification = "MSTest discovers public test classes with the repository discovery configuration.")]
public sealed partial class BuiltinCatalogDiscoveryTests
{
    /// <summary>Gets or sets the current test context.</summary>
    public TestContext TestContext { get; set; } = null!;

    /// <summary>Unchanged SDKs reuse captured metadata; reopening offline retains every native identity.</summary>
    /// <returns>The asynchronous cache and availability regression.</returns>
    [TestMethod]
    public async Task ValidCatalogIsReusedAndAvailableAfterSdkLoss()
    {
        using var fixture = new Fixture();
        var native = await ReadCatalogAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = fixture.Provider.Setup(value => value.GetBuiltinGeometryCatalogAsync(It.IsAny<string>(), "Content", It.IsAny<CancellationToken>(), It.IsAny<NativeArtifactLease>())).ReturnsAsync(native);
        using (var service = fixture.CreateService())
        {
            var first = await service.GetAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
            _ = first.IsCurrent.Should().BeTrue();
            _ = first.Notice.Should().BeNull();
            _ = (await service.RefreshAsync(this.TestContext.CancellationToken).ConfigureAwait(false)).Should().BeSameAs(first);
        }

        fixture.Provider.Verify(value => value.GetBuiltinGeometryCatalogAsync(It.IsAny<string>(), "Content", It.IsAny<CancellationToken>(), It.IsAny<NativeArtifactLease>()), Times.Once);
        var bytes = await File.ReadAllBytesAsync(fixture.CachePath, this.TestContext.CancellationToken).ConfigureAwait(false);
        File.Delete(fixture.ProducerPath);
        using var offline = fixture.CreateService();
        var cached = await offline.GetAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = cached.IsCurrent.Should().BeFalse();
        _ = cached.IsLastKnown.Should().BeTrue();
        _ = cached.Notice.Should().Contain("Preview unavailable");
        _ = cached.Catalog!.Geometries.Select(static definition => definition.AssetUri).Should().Equal(native.Geometries.Select(static definition => definition.AssetUri));
        _ = cached.Catalog.ToJson().Should().Be(native.ToJson());
        _ = (await File.ReadAllBytesAsync(fixture.CachePath, this.TestContext.CancellationToken).ConfigureAwait(false)).Should().Equal(bytes);
    }

    /// <summary>Unavailable SDKs cannot fabricate engine choices from absent or malformed cache files.</summary>
    /// <param name="malformed">Whether an invalid cache exists.</param>
    /// <returns>The asynchronous unavailable-catalog regression.</returns>
    [TestMethod]
    [DataRow(false)]
    [DataRow(true)]
    public async Task UnavailableSdkWithoutValidCacheHasNoCatalog(bool malformed)
    {
        using var fixture = new Fixture();
        if (malformed)
        {
            Directory.CreateDirectory(Path.GetDirectoryName(fixture.CachePath)!);
            await File.WriteAllTextAsync(fixture.CachePath, "invalid json", this.TestContext.CancellationToken).ConfigureAwait(false);
        }

        File.Delete(fixture.ProducerPath);
        using var service = fixture.CreateService();
        var result = await service.GetAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = result.Catalog.Should().BeNull();
        _ = result.IsLastKnown.Should().BeFalse();
        _ = result.Notice.Should().Contain("No last-known catalog");
        fixture.Provider.VerifyNoOtherCalls();
    }

    /// <summary>Repairing the SDK replaces the last-known state through an ordinary refresh.</summary>
    /// <returns>The asynchronous recovery regression.</returns>
    [TestMethod]
    public async Task RepairedSdkReplacesTheCachedAvailabilityState()
    {
        using var fixture = new Fixture();
        var native = await ReadCatalogAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = fixture.Provider.Setup(value => value.GetBuiltinGeometryCatalogAsync(It.IsAny<string>(), "Content", It.IsAny<CancellationToken>(), It.IsAny<NativeArtifactLease>())).ReturnsAsync(native);
        using var service = fixture.CreateService();
        _ = await service.GetAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        File.Delete(fixture.ProducerPath);
        _ = (await service.RefreshAsync(this.TestContext.CancellationToken).ConfigureAwait(false)).IsLastKnown.Should().BeTrue();
        await File.WriteAllTextAsync(fixture.ProducerPath, "producer", this.TestContext.CancellationToken).ConfigureAwait(false);
        var restored = await service.RefreshAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = restored.IsCurrent.Should().BeTrue();
        _ = restored.Notice.Should().BeNull();
        fixture.Provider.Verify(value => value.GetBuiltinGeometryCatalogAsync(It.IsAny<string>(), "Content", It.IsAny<CancellationToken>(), It.IsAny<NativeArtifactLease>()), Times.Exactly(2));
    }

    /// <summary>One caller's cancellation does not cancel catalog discovery for other consumers.</summary>
    /// <returns>The asynchronous coalescing regression.</returns>
    [TestMethod]
    public async Task ConcurrentCallersShareNativeWorkAndCancelTheirOwnWaits()
    {
        using var fixture = new Fixture();
        var native = await ReadCatalogAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        var entered = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var release = new TaskCompletionSource<BuiltinGeometryCatalog>(TaskCreationOptions.RunContinuationsAsynchronously);
        _ = fixture.Provider.Setup(value => value.GetBuiltinGeometryCatalogAsync(It.IsAny<string>(), "Content", It.IsAny<CancellationToken>(), It.IsAny<NativeArtifactLease>()))
            .Returns((string _, string _, CancellationToken token, NativeArtifactLease _) =>
            {
                _ = entered.TrySetResult();
                return release.Task.WaitAsync(token);
            });
        using var service = fixture.CreateService();
        using var cancellation = new CancellationTokenSource();
        var first = service.GetAsync(cancellation.Token);
        await entered.Task.WaitAsync(TimeSpan.FromSeconds(5), this.TestContext.CancellationToken).ConfigureAwait(false);
        var second = service.GetAsync(this.TestContext.CancellationToken);
        await cancellation.CancelAsync().ConfigureAwait(false);
        _ = await ((Func<Task>)(() => first)).Should().ThrowAsync<OperationCanceledException>().ConfigureAwait(false);
        release.SetResult(native);
        _ = (await second.ConfigureAwait(false)).IsCurrent.Should().BeTrue();
        fixture.Provider.Verify(value => value.GetBuiltinGeometryCatalogAsync(It.IsAny<string>(), "Content", It.IsAny<CancellationToken>(), It.IsAny<NativeArtifactLease>()), Times.Once);
    }

    /// <summary>A native worker that cannot stop retains its artifact lease until actual drain.</summary>
    /// <returns>The asynchronous retained-worker ownership regression.</returns>
    [TestMethod]
    public async Task ClosingDiscoveryRetainsArtifactsUntilTheWorkerDrains()
    {
        using var fixture = new Fixture();
        var entered = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var drain = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        _ = fixture.Provider.Setup(value => value.GetBuiltinGeometryCatalogAsync(It.IsAny<string>(), "Content", It.IsAny<CancellationToken>(), It.IsAny<NativeArtifactLease>()))
            .Returns((string _, string _, CancellationToken _, NativeArtifactLease _) =>
            {
                _ = entered.TrySetResult();
                return Task.FromException<BuiltinGeometryCatalog>(new ContentPipelineTerminationException(new IOException("worker still stopping"), drain.Task));
            });
        using var service = fixture.CreateService();
        var pending = service.GetAsync(this.TestContext.CancellationToken);
        await entered.Task.WaitAsync(TimeSpan.FromSeconds(5), this.TestContext.CancellationToken).ConfigureAwait(false);
        service.Dispose();
        Action replaceProducer = () => File.WriteAllText(fixture.ProducerPath, "replacement");
        _ = replaceProducer.Should().Throw<IOException>();
        _ = pending.IsCompleted.Should().BeFalse();
        drain.SetResult();
        _ = await ((Func<Task>)(() => pending)).Should().ThrowAsync<OperationCanceledException>().ConfigureAwait(false);
        replaceProducer();
        _ = File.Exists(fixture.CachePath).Should().BeFalse();
    }

    /// <summary>Invalid native metadata cannot replace the last valid cache.</summary>
    /// <returns>The asynchronous rejected-catalog regression.</returns>
    [TestMethod]
    public async Task InvalidNativeCatalogLeavesTheLastKnownDocumentIntact()
    {
        using var fixture = new Fixture();
        var native = await ReadCatalogAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = fixture.Provider.Setup(value => value.GetBuiltinGeometryCatalogAsync(It.IsAny<string>(), "Content", It.IsAny<CancellationToken>(), It.IsAny<NativeArtifactLease>())).ReturnsAsync(native);
        using (var service = fixture.CreateService())
        {
            _ = await service.GetAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        }

        var before = await File.ReadAllBytesAsync(fixture.CachePath, this.TestContext.CancellationToken).ConfigureAwait(false);
        var invalid = JsonNode.Parse(native.ToJson())!;
        invalid["geometries"]![0]!["asset_uri"] = "asset:///Content/Injected";
        _ = fixture.Provider.Setup(value => value.GetBuiltinGeometryCatalogAsync(It.IsAny<string>(), "Content", It.IsAny<CancellationToken>(), It.IsAny<NativeArtifactLease>()))
            .ReturnsAsync(BuiltinGeometryCatalog.Parse(invalid.ToJsonString()));
        using var retry = fixture.CreateService();
        var result = await retry.GetAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = result.IsLastKnown.Should().BeTrue();
        _ = result.Catalog!.ToJson().Should().Be(native.ToJson());
        _ = (await File.ReadAllBytesAsync(fixture.CachePath, this.TestContext.CancellationToken).ConfigureAwait(false)).Should().Equal(before);
    }

    /// <summary>A cache write failure must not hide freshly validated engine choices.</summary>
    /// <returns>The asynchronous read-only-cache regression.</returns>
    [TestMethod]
    public async Task ReadOnlyCacheDoesNotDiscardTheCurrentNativeCatalog()
    {
        using var fixture = new Fixture();
        var native = await ReadCatalogAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = fixture.Provider.Setup(value => value.GetBuiltinGeometryCatalogAsync(It.IsAny<string>(), "Content", It.IsAny<CancellationToken>(), It.IsAny<NativeArtifactLease>())).ReturnsAsync(native);
        var files = new Mock<IAtomicFileStore>();
        _ = files.Setup(value => value.ReadAsync(It.IsAny<string>(), It.IsAny<CancellationToken>())).ReturnsAsync(new FileSnapshot([], FileVersion.Missing));
        _ = files.Setup(value => value.WriteAsync(It.IsAny<string>(), It.IsAny<ReadOnlyMemory<byte>>(), It.IsAny<FileVersion>(), It.IsAny<CancellationToken>())).ThrowsAsync(new UnauthorizedAccessException("Read-only cache"));
        using var service = fixture.CreateService(files.Object);
        var result = await service.GetAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = result.IsCurrent.Should().BeTrue();
        _ = result.Catalog.Should().BeSameAs(native);
    }

    private static async Task<BuiltinGeometryCatalog> ReadCatalogAsync(CancellationToken cancellationToken)
        => BuiltinGeometryCatalog.Parse(await File.ReadAllTextAsync(Path.Combine(AppContext.BaseDirectory, "Fixtures", "BuiltinGeometryCatalog.json"), cancellationToken).ConfigureAwait(false));

    private sealed partial class Fixture : IDisposable
    {
        private readonly DirectoryInfo directory = Directory.CreateTempSubdirectory("oxygen-builtin-discovery-");
        private readonly Oxygen.Testing.TemporaryNativeArtifacts native;

        public Fixture()
        {
            File.WriteAllText(this.ProducerPath, "producer");
            this.native = new([new NativeArtifactLocation("producer", this.ProducerPath)]);
        }

        public string ProducerPath => Path.Combine(this.directory.FullName, "producer.bin");

        public string CachePath => Path.Combine(this.directory.FullName, "cache", "builtins", EditorNativeCompatibilityService.CurrentConfiguration, "catalog.json");

        public Mock<IBuiltinGeometryCatalogProvider> Provider { get; } = new(MockBehavior.Strict);

        public BuiltinCatalogDiscovery CreateService(IAtomicFileStore? files = null)
        {
            var paths = Mock.Of<IPathFinder>(value => value.LocalAppState == this.directory.FullName && value.Temp == this.directory.FullName);
            return new(this.Provider.Object, files ?? new NativeAtomicFileStore(new Testably.Abstractions.RealFileSystem()), paths, NullLogger<BuiltinCatalogDiscovery>.Instance, this.native);
        }

        public void Dispose()
        {
            this.native.Dispose();
            this.directory.Delete(recursive: true);
        }
    }
}
