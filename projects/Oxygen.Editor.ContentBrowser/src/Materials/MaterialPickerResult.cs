// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.ContentBrowser.AssetIdentity;

namespace Oxygen.Editor.ContentBrowser.Materials;

/// <summary>
/// One selectable material row returned by the material picker.
/// </summary>
/// <param name="MaterialUri">The stable material asset URI used by scene assignments.</param>
/// <param name="DisplayName">The display name.</param>
/// <param name="PrimaryState">The primary user-facing asset state.</param>
/// <param name="DerivedState">The optional derived cooked/stale state.</param>
/// <param name="RuntimeAvailability">The runtime availability overlay.</param>
/// <param name="DescriptorPath">The descriptor path for diagnostics/open actions only.</param>
/// <param name="CookedPath">The cooked output path for diagnostics only.</param>
/// <param name="BaseColorPreview">The scalar base-color preview.</param>
public sealed record MaterialPickerResult(
    Uri MaterialUri,
    string DisplayName,
    AssetState PrimaryState,
    AssetState? DerivedState,
    AssetRuntimeAvailability RuntimeAvailability,
    string? DescriptorPath,
    string? CookedPath,
    MaterialPreviewColor? BaseColorPreview)
{
    /// <summary>
    /// Gets the compact display state using the shared browser badge precedence.
    /// </summary>
    public AssetState DisplayState
        => this.PrimaryState is AssetState.Broken or AssetState.Missing
            ? this.PrimaryState
            : this.DerivedState ?? this.PrimaryState;
}
