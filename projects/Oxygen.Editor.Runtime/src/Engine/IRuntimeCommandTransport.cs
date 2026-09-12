// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Runtime.Engine;

/// <summary>Internal native conversion boundary, substitutable without constructing native facades.</summary>
internal interface IRuntimeCommandTransport
{
    /// <summary>Occurs after native generation and target validation accepts a failure.</summary>
    public event EventHandler<RuntimeAssetLoadFailedEventArgs>? AssetLoadFailed;

    /// <summary>Performs the native Execute operation.</summary>
    /// <param name="request">The command and its operation identity.</param>
    public void Execute(RuntimeWorldRequest request);

    /// <summary>Performs the native ActivateSceneAsync operation.</summary>
    /// <param name="name">The name transport value.</param>
    /// <returns>The native acknowledgment task.</returns>
    public Task<bool> ActivateSceneAsync(string name);

    /// <summary>Performs the native CreateNodeAsync operation.</summary>
    /// <param name="command">The command transport value.</param>
    /// <returns>The native acknowledgment task.</returns>
    public Task CreateNodeAsync(RuntimeCreateNode command);

    /// <summary>Observes native background after earlier scene mutations.</summary>
    /// <returns>The native background state.</returns>
    public Task<RuntimeBackgroundState> ObserveBackgroundAsync();

    /// <summary>Reads native atmosphere and post-process values after earlier mutations.</summary>
    /// <returns>The native environment state.</returns>
    public Task<RuntimeEnvironmentState> ObserveEnvironmentAsync();

    /// <summary>Reads node properties and currently resolved assets after earlier mutations.</summary>
    /// <param name="nodeId">The authored node identity.</param>
    /// <returns>The native node state.</returns>
    public Task<RuntimeNodeState> ObserveNodeAsync(Guid nodeId);

    /// <summary>Performs the native ExecuteInput operation.</summary>
    /// <param name="viewId">The viewId transport value.</param>
    /// <param name="input">The input transport value.</param>
    public void ExecuteInput(ulong viewId, RuntimeInputEvent input);

    /// <summary>Performs the native MountCookedRoot operation.</summary>
    /// <param name="path">The path transport value.</param>
    public void MountCookedRoot(string path);

    /// <summary>Performs the native ClearCookedRoots operation.</summary>
    public void ClearCookedRoots();

    /// <summary>Replaces the complete native loose-root set and refreshes current scene bindings.</summary>
    /// <param name="paths">All project roots that remain mounted after publication.</param>
    /// <returns>Completion after current native bindings settle.</returns>
    public Task ReplaceCookedRootsAsync(IReadOnlyList<string> paths);

    /// <summary>Pauses and drains cooked-content reads, or resumes current bindings and rendering.</summary>
    /// <param name="paused">Whether published files are about to be replaced.</param>
    /// <returns>The native phase acknowledgement.</returns>
    public Task SetCookedContentPausedAsync(bool paused);
}
