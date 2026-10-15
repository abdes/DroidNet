// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Assets.Runtime;

/// <summary>
///     Reports what the running preview has acknowledged about cooked content, so that asset states can
///     include preview availability without the asset services depending on the engine.
/// </summary>
/// <remarks>
///     The asset services own this contract; the module that drives the engine implements it.
/// </remarks>
public interface IAssetRuntimeStatusSource
{
    /// <summary>Occurs when the preview's content, its run or the outcome of an asset request changes.</summary>
    public event EventHandler? Changed;

    /// <summary>Gets the preview's current content state and the asset requests of one project.</summary>
    /// <param name="projectId">The project whose asset requests are reported; <see langword="null"/> reports none.</param>
    /// <returns>The status of the current preview run.</returns>
    public AssetRuntimeStatus GetStatus(Guid? projectId);
}
