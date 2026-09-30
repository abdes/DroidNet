// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Reactive.Disposables;
using System.Runtime.InteropServices;
using System.Text.Json;
using AwesomeAssertions;
using DroidNet.TestHelpers;
using DroidNet.Tests;
using Microsoft.Extensions.Logging;
using Microsoft.UI.Xaml.Controls;
using Oxygen.Editor.Runtime.Engine;

namespace Oxygen.Editor.World.Tests;

/// <summary>Supplies matched editor viewports and cooked inputs for pixel qualification.</summary>
public sealed partial class InspectorControlTests
{
    private static readonly JsonSerializerOptions IblImageJsonOptions = new() { WriteIndented = true, IncludeFields = true };

    /// <summary>Two identical views reach current IBL; IblCapture=true retains a readiness-triggered GPU capture.</summary>
    /// <returns>The asynchronous viewport qualification.</returns>
    [TestMethod]
    public Task MatchedIblViewportsExposeCurrentProductsForImageQualification() => EnqueueAsync(async () =>
    {
        var output = Directory.CreateTempSubdirectory("OxygenIblImages-");
        using var logs = new TestLoggerProvider();
        using var loggerFactory = Microsoft.Extensions.Logging.LoggerFactory.Create(builder => builder.AddProvider(logs));
        var captureRequested = this.TestContext.Properties.TryGetValue("IblCapture", out var capture)
            && string.Equals(capture as string, "true", StringComparison.OrdinalIgnoreCase);
        var settings = new EngineSettings();
        if (captureRequested)
        {
            settings.Graphics.FrameCapture = new()
            {
                Provider = FrameCaptureProvider.RenderDoc,
                InitMode = FrameCaptureInitMode.Search,
                FrameCount = 0,
                CaptureFileTemplate = Path.Combine(output.FullName, "editor"),
            };
        }

        try
        {
            await this.RunMatchedIblViewsAsync(output.FullName, settings, captureRequested, loggerFactory).ConfigureAwait(true);
        }
        finally
        {
            await File.WriteAllLinesAsync(Path.Combine(output.FullName, "native.log"), logs.Messages).ConfigureAwait(true);
            foreach (var path in Directory.EnumerateFiles(output.FullName, "*", SearchOption.AllDirectories))
            {
                this.TestContext.AddResultFile(path);
            }
        }
    });

    private async Task RunMatchedIblViewsAsync(string output, EngineSettings settings, bool captureRequested, ILoggerFactory loggerFactory)
    {
        var fixture = new NativeSceneFixture(automatic: false, scene => SeedShadowTransitionScene(scene, 1), settings, loggerFactory);
        await using var lifetime = fixture.ConfigureAwait(true);
        using var timeout = CancellationTokenSource.CreateLinkedTokenSource(this.TestContext.CancellationToken);
        timeout.CancelAfter(TimeSpan.FromSeconds(120));
        _ = (await fixture.Commands.SaveSceneAsync(fixture.Context).ConfigureAwait(true)).Succeeded.Should().BeTrue();
        using var services = new CatalogWorkloadServices(fixture);
        var sceneUri = new Uri("asset:///Content/Scenes/" + Uri.EscapeDataString(fixture.Source.Name) + ".oscene.json");
        _ = (await services.Pipeline.CookCurrentSceneAsync(sceneUri, timeout.Token).ConfigureAwait(true)).IsPublished.Should().BeTrue();
        await fixture.InitializeAsync(timeout.Token, mountPublished: true).ConfigureAwait(true);
        var geometry = await AssertGeometryAsync(fixture, fixture.Source.RootNodes[0].Id, "Cube", timeout.Token).ConfigureAwait(true);
        var first = new SwapChainPanel { Width = 320, Height = 240 };
        var second = new SwapChainPanel { Width = 320, Height = 240 };
        var host = new StackPanel { Orientation = Orientation.Horizontal, Children = { first, second } };
        var window = VisualUserInterfaceTestsApp.MainWindow;
        var previousSize = window.AppWindow.Size;
        using var restoreSize = Disposable.Create(() => window.AppWindow.Resize(previousSize));
        await LoadTestContentAsync(host).ConfigureAwait(true);
        var scale = host.XamlRoot.RasterizationScale;
        var pixelWidth = (uint)Math.Round(320 * scale);
        var pixelHeight = (uint)Math.Round(240 * scale);
        window.AppWindow.Resize(new((int)Math.Ceiling(700 * scale), (int)Math.Ceiling(300 * scale)));
        window.Activate();
        var firstRequest = new ViewportSurfaceRequest { DocumentId = fixture.Context.DocumentId, ViewportId = Guid.NewGuid(), ViewportIndex = 0, IsPrimary = true };
        var secondRequest = firstRequest with { ViewportId = Guid.NewGuid(), ViewportIndex = 1, IsPrimary = false };
        var firstSurface = await fixture.Runtime.AttachViewportAsync(firstRequest, first, timeout.Token).ConfigureAwait(true);
        await using var firstLifetime = firstSurface.ConfigureAwait(true);
        var secondSurface = await fixture.Runtime.AttachViewportAsync(secondRequest, second, timeout.Token).ConfigureAwait(true);
        await using var secondLifetime = secondSurface.ConfigureAwait(true);
        await firstSurface.ResizeAsync(pixelWidth, pixelHeight, timeout.Token).ConfigureAwait(true);
        await secondSurface.ResizeAsync(pixelWidth, pixelHeight, timeout.Token).ConfigureAwait(true);
        var views = new List<RuntimeViewId>();
        try
        {
            foreach (var request in new[] { firstRequest, secondRequest })
            {
                var view = await fixture.Runtime.CreateViewAsync(new()
                {
                    Name = "IBL reference " + request.ViewportIndex,
                    Purpose = "Viewport",
                    CompositingTarget = request.ViewportId,
                    Width = pixelWidth,
                    Height = pixelHeight,
                }).ConfigureAwait(true);
                _ = view.IsValid.Should().BeTrue();
                views.Add(view);
            }

            var cooked = await fixture.GetCookedRootAsync(fixture.ProjectRoot, timeout.Token).ConfigureAwait(true);
            await this.RecordIblImageInputsAsync(fixture, output, cooked, geometry, views.Count, (pixelWidth, pixelHeight), captureRequested, timeout.Token).ConfigureAwait(true);
        }
        finally
        {
            foreach (var view in views)
            {
                _ = await fixture.Runtime.DestroyViewAsync(view).ConfigureAwait(true);
            }
        }
    }

    private async Task RecordIblImageInputsAsync(
        NativeSceneFixture fixture, string output, string cooked, RuntimeNodeState geometry, int viewCount, (uint width, uint height) extent, bool captureRequested, CancellationToken cancellationToken)
    {
        var ready = await this.WaitForCurrentSkyAsync("matched viewports ready", fixture, _ => true, cancellationToken).ConfigureAwait(true);
        await ObserveRenderedFramesAsync(fixture, cancellationToken).ConfigureAwait(true);
        if (captureRequested)
        {
            using var capture = new IblRenderDocCapture();
            var previousCount = capture.Count;
            capture.Begin();
            try
            {
                var started = await fixture.ReadNativeAsync(cancellationToken).ConfigureAwait(true);
                _ = await this.WaitForCurrentSkyAsync("captured complete frames", fixture, state => state.SkyLightFrameSequence > started.SkyLightFrameSequence + 3, cancellationToken).ConfigureAwait(true);
            }
            finally
            {
                capture.End();
            }

            _ = capture.Count.Should().Be(previousCount + 1);

            _ = Directory.EnumerateFiles(output, "*.rdc").Should().ContainSingle();
        }

        var record = new { RequestedWidth = extent.width, RequestedHeight = extent.height, ViewCount = viewCount, Captured = captureRequested, Environment = fixture.Source.Environment, Geometry = geometry, Ready = ready };
        await File.WriteAllTextAsync(Path.Combine(output, "inputs.json"), JsonSerializer.Serialize(record, IblImageJsonOptions), cancellationToken).ConfigureAwait(true);
        foreach (var (root, name) in new[] { (Path.Combine(fixture.ProjectRoot, "Content"), "source"), (Path.GetDirectoryName(cooked)!, "cooked") })
        {
            var target = Path.Combine(output, name);
            foreach (var path in Directory.EnumerateFiles(root, "*", SearchOption.AllDirectories))
            {
                var destination = Path.Combine(target, Path.GetRelativePath(root, path));
                _ = Directory.CreateDirectory(Path.GetDirectoryName(destination)!);
                File.Copy(path, destination);
            }
        }
    }

    // RenderDoc is loaded by Graphics before device creation. This test adapter
    // captures complete engine frames across both swapchain presents using its
    // public 1.0 API after renderer readiness.
    private sealed class IblRenderDocCapture : IDisposable
    {
        private readonly nint module;
        private readonly CountCaptures count;
        private readonly StartCapture start;
        private readonly CountCaptures isCapturing;
        private readonly EndCapture end;

        public IblRenderDocCapture()
        {
            this.module = NativeLibrary.Load("renderdoc.dll");
            try
            {
                var getApi = Marshal.GetDelegateForFunctionPointer<GetApi>(NativeLibrary.GetExport(this.module, "RENDERDOC_GetAPI"));
                _ = getApi(10000, out var api).Should().Be(1);

                // Stable RENDERDOC_API_1_0_0 slots from renderdoc_app.h.
                this.count = Marshal.GetDelegateForFunctionPointer<CountCaptures>(Marshal.ReadIntPtr(api, 13 * nint.Size));
                this.start = Marshal.GetDelegateForFunctionPointer<StartCapture>(Marshal.ReadIntPtr(api, 19 * nint.Size));
                this.isCapturing = Marshal.GetDelegateForFunctionPointer<CountCaptures>(Marshal.ReadIntPtr(api, 20 * nint.Size));
                this.end = Marshal.GetDelegateForFunctionPointer<EndCapture>(Marshal.ReadIntPtr(api, 21 * nint.Size));
            }
            catch
            {
                NativeLibrary.Free(this.module);
                throw;
            }
        }

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        private delegate int GetApi(int version, out nint api);

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        private delegate uint CountCaptures();

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        private delegate void StartCapture(nint device, nint window);

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        private delegate uint EndCapture(nint device, nint window);

        public uint Count => this.count();

        public void Begin()
        {
            _ = this.isCapturing().Should().Be(0);
            this.start(0, 0);
            _ = this.isCapturing().Should().Be(1);
        }

        public void End() => this.end(0, 0).Should().Be(1);

        public void Dispose() => NativeLibrary.Free(this.module);
    }
}
