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

    /// <summary>Performs the native ExecuteInput operation.</summary>
    /// <param name="viewId">The viewId transport value.</param>
    /// <param name="input">The input transport value.</param>
    public void ExecuteInput(ulong viewId, RuntimeInputEvent input);

    /// <summary>Performs the native MountCookedRoot operation.</summary>
    /// <param name="path">The path transport value.</param>
    public void MountCookedRoot(string path);

    /// <summary>Performs the native ClearCookedRoots operation.</summary>
    public void ClearCookedRoots();
}
