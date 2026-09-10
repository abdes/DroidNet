// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Runtime.InteropServices;
using DroidNet.Hosting.WinUI;
using Microsoft.Extensions.Logging;
using Microsoft.UI.Xaml.Controls;
using Oxygen.Interop;
using Oxygen.Interop.Input;
using Oxygen.Interop.World;

namespace Oxygen.Editor.Runtime.Engine;

/// <summary>Retains each native owner until its deterministic destruction succeeds.</summary>
/// <param name="hostingContext">The owning UI dispatcher, which must remain alive through native cleanup.</param>
[System.Diagnostics.CodeAnalysis.SuppressMessage("Design", "CA1001:Types that own disposable fields should be disposable", Justification = "The service invokes explicit destruction stages independently and retains failed ownership for retry.")]
internal sealed class NativeEngineSession(HostingContext hostingContext) : EngineSession
{
    private EngineRunner? runner;
    private EngineContext? context;
    private OxygenWorld world = null!;
    private OxygenInput input = null!;

    /// <inheritdoc/>
    public override EngineRunner Runner => this.runner ?? throw new InvalidOperationException("Engine runner is unavailable.");

    /// <inheritdoc/>
    public override EngineContext? Context => this.context;

    /// <inheritdoc/>
    public override OxygenWorld World => this.world;

    /// <inheritdoc/>
    public override OxygenInput Input => this.input;

    /// <inheritdoc/>
    public override bool HasRunner => this.runner is not null;

    /// <inheritdoc/>
    public override bool HasContext => this.context is not null;

    /// <inheritdoc/>
    public override void Initialize(EditorEngineConfigManaged config, ILogger? logger)
    {
        this.runner = new EngineRunner();
        if (logger is not null)
        {
            _ = this.runner.ConfigureLogging(
                new LoggingConfig
                {
                    Verbosity = 0,
                    IsColored = false,
                    ModuleOverrides = string.Empty,
                },
                logger);
        }

        this.context = this.runner.CreateEngine(config);
        if (this.context?.IsValid != true)
        {
            throw new InvalidOperationException("Failed to create engine context.");
        }

        this.world = new OxygenWorld(this.context);
        this.input = new OxygenInput(this.context);
        this.runner.SetTargetFps(this.context, Math.Clamp(config.Engine.TargetFps, 0, EngineConfig.MaxTargetFps));
    }

    /// <inheritdoc/>
    public override Task RunAsync() => this.Runner.RunEngineAsync(this.context);

    /// <inheritdoc/>
    public override void Stop() => this.Runner.StopEngine(this.context);

    /// <inheritdoc/>
    public override Task CompleteLoopCleanupAsync()
        => hostingContext.Dispatcher.DispatchAsync(this.Runner.WaitForLoopCleanupAsync);

    /// <inheritdoc/>
    public override async Task<bool> RegisterSurfaceAsync(ViewportSurfaceKey key, SwapChainPanel panel)
    {
        var panelPointer = Marshal.GetIUnknownForObject(panel);
        try
        {
            var (scale, width, height) = GetInitialDimensions(panel);
            return await this.Runner.TryRegisterSurfaceAsync(
                this.context,
                key.DocumentId,
                key.ViewportId,
                key.DisplayName,
                panelPointer,
                width,
                height,
                scale).ConfigureAwait(true);
        }
        finally
        {
            _ = Marshal.Release(panelPointer);
        }
    }

    /// <inheritdoc/>
    public override Task<bool> UnregisterSurfaceAsync(Guid viewportId) => this.Runner.TryUnregisterSurfaceAsync(viewportId);

    /// <inheritdoc/>
    public override Task<bool> ResizeSurfaceAsync(Guid viewportId, uint width, uint height)
        => this.Runner.TryResizeSurfaceAsync(viewportId, width, height);

    /// <inheritdoc/>
    public override void DestroyContext()
    {
        this.context?.Dispose();
        this.context = null;
        this.world = null!;
        this.input = null!;
    }

    /// <inheritdoc/>
    public override void DestroyRunner()
    {
        if (this.runner is not null && !hostingContext.Dispatcher.HasThreadAccess)
        {
            throw new InvalidOperationException("Native runner destruction requires its UI dispatcher thread.");
        }

        this.runner?.Dispose();
        this.runner = null;
    }

    private static (float scale, uint width, uint height) GetInitialDimensions(SwapChainPanel panel)
    {
        var scale = (float)(panel.XamlRoot?.RasterizationScale ?? 1.0);
        var width = Math.Round(panel.ActualWidth * scale);
        var height = Math.Round(panel.ActualHeight * scale);
        if (!double.IsFinite(width) || !double.IsFinite(height) || width < 1 || height < 1)
        {
            return (1, 0, 0);
        }

        return (scale, Convert.ToUInt32(Math.Min(uint.MaxValue, width)), Convert.ToUInt32(Math.Min(uint.MaxValue, height)));
    }
}
