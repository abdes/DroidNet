// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using AwesomeAssertions;
using DroidNet.Storage.Native;
using Microsoft.Extensions.Logging.Abstractions;
using Moq;
using Oxygen.Editor.ContentBrowser.AssetIdentity;
using Oxygen.Editor.ContentBrowser.Infrastructure.Assets;
using Oxygen.Editor.ContentPipeline;
using Oxygen.Editor.ContentPipeline.Publication;
using Oxygen.Editor.ContentPipeline.Snapshots;
using Oxygen.Editor.ContentPipeline.Status;
using Oxygen.Managed.Core.Compatibility;
using Testably.Abstractions;

namespace Oxygen.Editor.ContentBrowser.Tests;

/// <summary>Measures the production catalog/status projection separately from live viewport qualification.</summary>
public sealed partial class ContentBrowserAssetProviderTests
{
    /// <summary>One thousand real scalar descriptors load and filter without starting native work.</summary>
    /// <returns>The asynchronous component workload measurement.</returns>
    [TestMethod]
    [TestCategory("Performance")]
    public async Task ThousandDescriptorCatalogKeepsWarmQueriesLocal()
    {
        using var workspace = new TempWorkspace();
        for (var index = 0; index < 1000; index++)
        {
            WriteMaterial(workspace.SourcePath(string.Create(System.Globalization.CultureInfo.InvariantCulture, $"Content/Materials/Group{index % 10}/M{index:D4}.omat.json")));
        }

        var projects = CreateProjectContextService(workspace);
        var documents = new CookDocumentRegistry();
        using var runs = new ContentCookCoordinator(projects, NullLogger<ContentCookCoordinator>.Instance);
        var files = new NativeAtomicFileStore(new RealFileSystem());
        var native = new Mock<INativeCompatibilityService>(MockBehavior.Strict);
        var statuses = new AssetCookStatusReader(documents, new CookPublicationService(runs, projects, files), native.Object, files);
        using var catalog = new ProjectAssetCatalog(projects, new NativeStorageProvider(new RealFileSystem()), CreateEmptyImportBuiltins());
        var runtime = Oxygen.Testing.AssetStatusFixture.CreateUnavailableRuntime();
        await using var runtimeLifetime = runtime.ConfigureAwait(false);
        using var provider = new ContentBrowserAssetProvider(catalog, projects, new TestProjectCookScopeProvider(workspace), new AssetIdentityReducer(), statuses, documents, runs, runtime);
        IReadOnlyList<ContentBrowserAssetItem> rows = [];
        provider.Items.Subscribe(new Observer<IReadOnlyList<ContentBrowserAssetItem>>(value => rows = value), this.TestContext.CancellationToken);
        var watch = Stopwatch.StartNew();
        await provider.RefreshAsync(AssetBrowserFilter.Default, this.TestContext.CancellationToken).ConfigureAwait(false);
        var cold = watch.Elapsed;
        _ = rows.Should().HaveCount(1000);
        _ = rows.Should().OnlyContain(static row => row.CookStatus != null && row.CookStatus.Freshness == AssetCookFreshness.NeedsCooking);
        var query = new AssetBrowserQuery();
        var samples = new List<double>();
        for (var index = 0; index < 110; index++)
        {
            watch.Restart();
            query.SearchText = "Group" + (index % 10).ToString(System.Globalization.CultureInfo.InvariantCulture);
            var visible = rows.Where(query.Matches).ToArray();
            var elapsed = watch.Elapsed.TotalMilliseconds;
            _ = visible.Should().HaveCount(100);
            if (index >= 10)
            {
                samples.Add(elapsed);
            }
        }

        samples.Sort();
        this.TestContext.WriteLine(string.Create(System.Globalization.CultureInfo.InvariantCulture, $"Catalog/status component: cold={cold.TotalMilliseconds:F2}ms; warm-query p95={samples[94]:F2}ms; entries={rows.Count}; configuration={typeof(ContentBrowserAssetProvider).Assembly.GetCustomAttributes(typeof(System.Reflection.AssemblyConfigurationAttribute), inherit: false).Cast<System.Reflection.AssemblyConfigurationAttribute>().Single().Configuration}"));
        _ = cold.Should().BeLessThan(TimeSpan.FromSeconds(5));
        _ = samples[94].Should().BeLessThanOrEqualTo(250);
        _ = runs.Runs.Should().BeEmpty();
        native.VerifyNoOtherCalls();
    }
}
