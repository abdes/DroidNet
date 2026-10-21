// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Managed.Core;

namespace Oxygen.Editor.Shell.Windows;

/// <summary>Shows assets of the window's project in the panel that browses them.</summary>
/// <remarks>The browsing feature implements it, so a part that wants to show an asset needs no reference to it.</remarks>
public interface IAssetReveal
{
    /// <summary>Shows assets, bringing the panel that browses them forward.</summary>
    /// <param name="assets">The assets to show; at least one.</param>
    /// <returns><see langword="true"/> when the assets were shown; <see langword="false"/> when none of them is available in the project.</returns>
    public Task<bool> RevealAsync(IReadOnlyList<AssetPath> assets);
}
