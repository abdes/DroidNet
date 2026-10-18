// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using AwesomeAssertions;
using DroidNet.Storage.Native;
using Microsoft.Extensions.Logging.Abstractions;
using Moq;
using Oxygen.Editor.Assets.TestSupport;
using Oxygen.Editor.ContentPipeline;
using Oxygen.Editor.ContentPipeline.Authoring;
using Oxygen.Editor.ContentPipeline.Discovery;
using Oxygen.Editor.ContentPipeline.Publication;
using Oxygen.Editor.ContentPipeline.Relocation;
using Oxygen.Editor.ContentPipeline.Snapshots;
using Oxygen.Editor.ContentPipeline.Status;
using Oxygen.Editor.Projects;
using Oxygen.Managed.Assets.Authoring.Materials;
using Oxygen.Managed.Core.Compatibility;
using Testably.Abstractions;

namespace Oxygen.Editor.Assets.Benchmarks.Tests;

/// <summary>Measures how long the asset feed takes to publish a large project.</summary>
[TestClass]
public sealed class AssetFeedWorkloadBenchmarks
{
    /// <summary>Gets or sets the running test context.</summary>
    public TestContext TestContext { get; set; } = null!;

    /// <summary>One thousand material sources are published with their cook status, without starting a cook or native work.</summary>
    /// <returns>The asynchronous workload measurement.</returns>
    [TestMethod]
    [TestCategory("Performance")]
    public async Task ThousandMaterialsArePublishedWithoutNativeWork()
    {
        using var workspace = new AssetWorkspace();
        for (var index = 0; index < 1000; index++)
        {
            WriteMaterial(workspace.SourcePath(string.Create(System.Globalization.CultureInfo.InvariantCulture, $"Content/Materials/Group{index % 10}/M{index:D4}.omat.json")));
        }

        var projects = workspace.ActivateProject();
        var documents = new CookDocumentRegistry();
        using var runs = new ContentCookCoordinator(projects, NullLogger<ContentCookCoordinator>.Instance);
        var files = new NativeAtomicFileStore(new RealFileSystem());
        var native = new Mock<INativeCompatibilityService>(MockBehavior.Strict);
        var content = new Mock<IEngineContentPipelineApi>(MockBehavior.Strict);
        var storage = new NativeStorageProvider(new RealFileSystem());
        var publication = new CookPublicationService(runs, projects, files, new ProjectManagerService(storage, atomicFiles: files));
        var statuses = new AssetCookStatusReader(documents, publication, native.Object);
        await using var catalog = new AssetCatalog(projects, storage, CreateEmptyImportBuiltins(), publication);
        var runtime = AssetStatusFixture.CreateUnavailableRuntimeStatus();
        using var provider = new AssetFeed(catalog, projects, new WorkspaceCookScopeProvider(workspace), statuses, documents, runs, runtime, Mock.Of<IAssetRelocationService>(), Mock.Of<IAssetAuthoring>());
        IReadOnlyList<AssetEntry> rows = [];
        provider.Items.Subscribe(new Observer<IReadOnlyList<AssetEntry>>(value => rows = value.Where(static row => row.Kind == AssetKind.Material).ToArray()), this.TestContext.CancellationToken);
        var watch = Stopwatch.StartNew();
        await provider.RefreshAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        var cold = watch.Elapsed;
        _ = rows.Should().HaveCount(1000);
        _ = rows.Should().OnlyContain(static row => row.CookStatus != null && row.CookStatus.Freshness == AssetCookFreshness.NeedsCooking);
        this.TestContext.WriteLine(string.Create(System.Globalization.CultureInfo.InvariantCulture, $"Asset feed: cold={cold.TotalMilliseconds:F2}ms; entries={rows.Count}; configuration={typeof(AssetFeed).Assembly.GetCustomAttributes(typeof(System.Reflection.AssemblyConfigurationAttribute), inherit: false).Cast<System.Reflection.AssemblyConfigurationAttribute>().Single().Configuration}"));
        _ = cold.Should().BeLessThan(TimeSpan.FromSeconds(5));
        _ = runs.Runs.Should().BeEmpty();
        native.VerifyAdd(service => service.ObservationChanged += It.IsAny<EventHandler>(), Times.Once);
        native.VerifyNoOtherCalls();
        content.VerifyNoOtherCalls();
    }

    private static IBuiltinCatalogDiscovery CreateEmptyImportBuiltins()
    {
        var empty = new BuiltinCatalogSnapshot(Catalog: null, IsLastKnown: false, Notice: null);
        var builtins = new Mock<IBuiltinCatalogDiscovery>();
        _ = builtins.SetupGet(value => value.Snapshot).Returns(empty);
        _ = builtins.Setup(value => value.GetAsync(It.IsAny<CancellationToken>())).ReturnsAsync(empty);
        return builtins.Object;
    }

    private static void WriteMaterial(string path)
    {
        _ = Directory.CreateDirectory(Path.GetDirectoryName(path)!);
        using var stream = File.Create(path);
        MaterialSourceWriter.Write(
            stream,
            new MaterialSource(
                "Material",
                new MaterialPbrMetallicRoughness(
                    1.0f,
                    0.0f,
                    0.0f,
                    1.0f,
                    metallicFactor: 0.0f,
                    roughnessFactor: 0.5f),
                alphaMode: MaterialAlphaMode.Opaque,
                alphaCutoff: 0.5f,
                doubleSided: false));
    }
}
