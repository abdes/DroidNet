// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Assets.Runtime;
using Oxygen.Editor.Runtime.Engine;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.LivePreview.Assets;

/// <summary>Reports the embedded engine's acknowledged content and asset requests to the asset services.</summary>
public sealed partial class AssetRuntimeStatusSource : IAssetRuntimeStatusSource, IDisposable
{
    private readonly IEngineService engine;
    private readonly IRuntimeWorldCommands world;
    private bool disposed;

    /// <summary>Initializes a new instance of the <see cref="AssetRuntimeStatusSource"/> class.</summary>
    /// <param name="engine">The embedded engine whose content and asset requests are reported.</param>
    public AssetRuntimeStatusSource(IEngineService engine)
    {
        ArgumentNullException.ThrowIfNull(engine);
        this.engine = engine;
        this.world = engine.WorldCommands;
        this.engine.ContentStatusChanged += this.OnContentStatusChanged;
        this.engine.StateChanged += this.OnStateChanged;
        this.world.AssetStatusChanged += this.OnAssetStatusChanged;
    }

    /// <inheritdoc />
    public event EventHandler? Changed;

    /// <inheritdoc />
    public AssetRuntimeStatus GetStatus(Guid? projectId)
    {
        var content = this.engine.ContentStatus;
        if (content.RunId == Guid.Empty || content.State is RuntimeContentState.Unavailable or RuntimeContentState.Failed)
        {
            return new(AssetRuntimeContentState.Unavailable, [], [], content.Reason);
        }

        AssetRuntimeRequest[] requests = projectId is not { } project ? [] : [.. this.world.AssetRequests
            .Where(status => status.Request.Target.RunId == content.RunId && status.Request.Target.ProjectId == project)
            .Select(static status => AssetPath.TryParse(EnginePath(status.Request.Command), out var asset) ? new AssetRuntimeRequest(asset, status.Succeeded, status.Reason) : null)
            .OfType<AssetRuntimeRequest>()];
        var state = content.State switch
        {
            RuntimeContentState.Unmounted => AssetRuntimeContentState.Unmounted,
            RuntimeContentState.Updating => AssetRuntimeContentState.Updating,
            RuntimeContentState.Mounted => AssetRuntimeContentState.Mounted,
            _ => throw new InvalidOperationException($"Unknown runtime content state {content.State}."),
        };
        return new(state, [.. content.Roots.Select(FileSystemPath.From)], requests, content.Reason);
    }

    /// <inheritdoc />
    public void Dispose()
    {
        if (this.disposed)
        {
            return;
        }

        this.disposed = true;
        this.engine.ContentStatusChanged -= this.OnContentStatusChanged;
        this.engine.StateChanged -= this.OnStateChanged;
        this.world.AssetStatusChanged -= this.OnAssetStatusChanged;
    }

    private static string? EnginePath(RuntimeWorldCommand command) => command switch
    {
        RuntimeSetGeometry geometry => geometry.AssetPath,
        RuntimeSetMaterialOverride material => material.MaterialPath,
        _ => null,
    };

    private void OnContentStatusChanged(object? sender, RuntimeContentChangedEventArgs args) => this.Changed?.Invoke(this, EventArgs.Empty);

    private void OnStateChanged(object? sender, EngineStateChangedEventArgs args) => this.Changed?.Invoke(this, EventArgs.Empty);

    private void OnAssetStatusChanged(object? sender, EventArgs args) => this.Changed?.Invoke(this, EventArgs.Empty);
}
