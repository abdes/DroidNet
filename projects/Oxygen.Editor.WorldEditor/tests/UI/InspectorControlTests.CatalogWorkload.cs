// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using System.Globalization;
using System.Reactive.Linq;
using AwesomeAssertions;
using CommunityToolkit.WinUI;
using DroidNet.Tests;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Oxygen.Editor.ContentBrowser;
using Oxygen.Editor.ContentBrowser.AssetIdentity;
using Oxygen.Editor.ContentBrowser.Infrastructure.Assets;
using Oxygen.Editor.ContentBrowser.Panes.Assets;
using Oxygen.Editor.ContentBrowser.Panes.Assets.Layouts;
using Oxygen.Editor.ContentPipeline.Status;
using Oxygen.Editor.Runtime.Engine;

namespace Oxygen.Editor.World.Tests;

/// <summary>Measures rendered browser feedback while a production native viewport draws the PRD scene workload.</summary>
public sealed partial class InspectorControlTests
{
    /// <summary>A cold production catalog and warm rendered filters meet the browser response budget with a live 100-node scene.</summary>
    /// <param name="tiles">Whether the rendered browser uses tiles.</param>
    /// <returns>The asynchronous browser and native viewport workload.</returns>
    [TestMethod]
    [TestCategory("Performance")]
    [DataRow(false)]
    [DataRow(true)]
    public Task ThousandInputBrowserFiltersWithLiveHundredNodeViewport(bool tiles) => EnqueueAsync(async () =>
    {
        var fixture = new NativeSceneFixture(automatic: false, engineSettings: new EngineSettings
        {
            Graphics = new() { EnableDebugLayer = false, EnableValidation = false },
            Renderer = new() { DirectionalShadowPolicy = DirectionalShadowPolicy.ConventionalOnly, EnableImGui = false },
            Timing = new() { FixedDelta = TimeSpan.FromSeconds(1.0 / 60) },
        });
        await using var lifetime = fixture.ConfigureAwait(true);
        using var timeout = CancellationTokenSource.CreateLinkedTokenSource(this.TestContext.CancellationToken);
        timeout.CancelAfter(TimeSpan.FromMinutes(5));
        using var services = new CatalogWorkloadServices(fixture);
        var definitions = (await services.Builtins.GetAsync(timeout.Token).ConfigureAwait(true)).Catalog!;
        _ = definitions.Should().NotBeNull();
        await SeedCatalogWorkloadAsync(fixture, services.Projects.ActiveProject!, services.Pipeline, definitions, timeout.Token).ConfigureAwait(true);
        await this.RecordCatalogWorkloadAsync(fixture, timeout.Token).ConfigureAwait(true);
        await fixture.InitializeAsync(timeout.Token, Path.Combine(fixture.ProjectRoot, ".cooked/Content")).ConfigureAwait(true);
        await fixture.RefreshCookedRootsAsync(Path.Combine(fixture.ProjectRoot, ".cooked/Content")).WaitAsync(timeout.Token).ConfigureAwait(true);
        _ = fixture.Runtime.ContentStatus.State.Should().Be(RuntimeContentState.Mounted);
        var geometryNodes = fixture.Source.RootNodes.SelectMany(static node => node.Descendants().Prepend(node)).Where(static node => node.Components.OfType<GeometryComponent>().Any()).ToArray();
        _ = geometryNodes.Should().HaveCount(98);
        ulong triangles = 0;
        foreach (var node in geometryNodes)
        {
            var state = await WaitForNodeAsync(fixture, node.Id, static value => value.IndexCount > 0, timeout.Token).ConfigureAwait(true);
            triangles += state.IndexCount / 3;
        }

        _ = triangles.Should().BeLessThanOrEqualTo(250000);
        this.TestContext.WriteLine(string.Create(CultureInfo.InvariantCulture, $"Loaded workload: geometry nodes={geometryNodes.Length}; triangles={triangles}"));
        await this.RunCatalogLayoutWorkloadAsync(fixture, services, tiles, triangles, timeout.Token).ConfigureAwait(true);
    });

    private async Task RunCatalogLayoutWorkloadAsync(NativeSceneFixture fixture, CatalogWorkloadServices services, bool tiles, ulong triangles, CancellationToken cancellationToken)
    {
        var initialRoot = new Grid();
        await LoadTestContentAsync(initialRoot).ConfigureAwait(true);
        var originalSize = VisualUserInterfaceTestsApp.MainWindow.AppWindow.Size;
        var scale = initialRoot.XamlRoot.RasterizationScale;
        VisualUserInterfaceTestsApp.MainWindow.AppWindow.Resize(new(2000, 1080 + (int)(360 * scale)));
        try
        {
            var panel = new SwapChainPanel { Width = 1920 / scale, Height = 1080 / scale };
            var state = new ContentBrowserState(services.Projects);
            state.SetSelectedFolders(["/Content"]);
            using var catalog = new ProjectAssetCatalog(services.Projects, services.Storage, services.Builtins);
            using var provider = new ContentBrowserAssetProvider(catalog, services.Projects, services.Scopes, new AssetIdentityReducer(), services.Pipeline, services.Documents, services.Runs, fixture.Runtime);
            using AssetsLayoutViewModel layout = tiles
                ? new TilesLayoutViewModel(provider, services.Projects, state, CreateStatusHosting(), services.Builtins)
                : new ListLayoutViewModel(provider, services.Projects, state, CreateStatusHosting(), services.Builtins);
            var query = new AssetQueryView { ViewModel = state.Query };
            UserControl view = tiles ? new TilesLayoutView { ViewModel = (TilesLayoutViewModel)layout } : new ListLayoutView { ViewModel = (ListLayoutViewModel)layout };
            var browser = CreateQueryTestRoot(query, view, light: false);
            browser.Width = 1920 / scale;
            browser.Height = 280;
            var root = new StackPanel();
            root.Children.Add(panel);
            root.Children.Add(browser);
            await LoadTestContentAsync(root).ConfigureAwait(true);
            var request = new ViewportSurfaceRequest { DocumentId = fixture.Source.Id, ViewportId = Guid.NewGuid(), ViewportIndex = 0, IsPrimary = true };
            var surface = await fixture.Runtime.AttachViewportAsync(request, panel, cancellationToken).ConfigureAwait(true);
            await using var surfaceLifetime = surface.ConfigureAwait(true);
            await surface.ResizeAsync(1920, 1080, cancellationToken).ConfigureAwait(true);
            var runtimeView = await fixture.Runtime.CreateViewAsync(new() { Name = "Catalog workload", Purpose = "Viewport", CompositingTarget = request.ViewportId, Width = 1920, Height = 1080 }).ConfigureAwait(true);
            try
            {
                _ = runtimeView.IsValid.Should().BeTrue();
                await ObserveRenderedFramesAsync(fixture, cancellationToken).ConfigureAwait(true);
                await this.MeasureCatalogFiltersAsync(provider, layout, query, view, browser, tiles, triangles, scale, cancellationToken).ConfigureAwait(true);
                _ = fixture.Runtime.State.Should().Be(EngineServiceState.Running);
            }
            finally
            {
                _ = await fixture.Runtime.DestroyViewAsync(runtimeView).ConfigureAwait(true);
            }
        }
        finally
        {
            await LoadTestContentAsync(new Grid()).ConfigureAwait(true);
            VisualUserInterfaceTestsApp.MainWindow.AppWindow.Resize(originalSize);
        }
    }

    private async Task MeasureCatalogFiltersAsync(ContentBrowserAssetProvider provider, AssetsLayoutViewModel layout, AssetQueryView query, UserControl view, Grid browser, bool tiles, ulong triangles, double scale, CancellationToken cancellationToken)
    {
        var watch = Stopwatch.StartNew();
        await layout.OnNavigatedToAsync(null!, null!).ConfigureAwait(true);
        await WaitForRenderAsync().WaitAsync(cancellationToken).ConfigureAwait(true);
        var cold = watch.Elapsed;
        var rows = await provider.Items.FirstAsync();
        _ = rows.Where(static row => !row.IsBuiltin && (row.DescriptorPath is not null || row.Kind == AssetKind.ForeignSource))
            .Should().HaveCount(1000).And.OnlyContain(static row => row.CookStatus != null && row.CookStatus.Freshness == AssetCookFreshness.Current);
        _ = rows.Should().NotContain(static row => row.RuntimeAvailability == AssetRuntimeAvailability.Updating);
        var selector = view.FindDescendant<ListViewBase>();
        _ = selector.Should().NotBeNull("the browser must realize its asset selector before timing feedback");
        _ = selector!.Items.Count.Should().BePositive();
        var samples = new List<double>();
        var search = (AutoSuggestBox)query.FindName("AssetSearch");
        for (var sample = 0; sample < 110; sample++)
        {
            watch.Restart();
            search.Text = "Group" + (sample % 10).ToString(CultureInfo.InvariantCulture);
            await WaitForRenderAsync().WaitAsync(cancellationToken).ConfigureAwait(true);
            var elapsed = watch.Elapsed.TotalMilliseconds;
            _ = layout.Assets.Should().HaveCount(sample % 10 < 8 ? 100 : 99);
            if (sample >= 10)
            {
                samples.Add(elapsed);
            }
        }

        samples.Sort();
        var configuration = typeof(ContentBrowserAssetProvider).Assembly.GetCustomAttributes(typeof(System.Reflection.AssemblyConfigurationAttribute), inherit: false).Cast<System.Reflection.AssemblyConfigurationAttribute>().Single().Configuration;
        this.TestContext.WriteLine(string.Create(CultureInfo.InvariantCulture, $"Rendered catalog: configuration={configuration}; tiles={tiles}; cold={cold.TotalMilliseconds:F2}ms; filter p95={samples[94]:F2}ms; rows={rows.Count}; nodes=100; triangles={triangles}; viewport=1920x1080; scale={scale}"));
        _ = cold.Should().BeLessThanOrEqualTo(TimeSpan.FromSeconds(5));
        _ = samples[94].Should().BeLessThanOrEqualTo(250);
        await this.CaptureComponentLayoutAsync(browser, "catalog-workload-" + tiles + ".png").ConfigureAwait(true);
    }
}
