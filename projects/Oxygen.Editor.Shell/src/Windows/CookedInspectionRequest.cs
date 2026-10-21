// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Managed.Assets.Catalog;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Shell.Windows;

/// <summary>What the cooked inspection panel is asked to inspect.</summary>
/// <param name="Scope">The folder or asset to inspect; <see langword="null"/> for the whole project.</param>
/// <param name="Validate">Whether root integrity is checked too.</param>
public sealed record CookedInspectionRequest(AssetPath? Scope, bool Validate)
{
    /// <summary>Gets an asset whose catalog facts should accompany the inspection of an output.</summary>
    public AssetPath? Asset { get; init; }

    /// <summary>Gets the physical representation explicitly selected in a library view.</summary>
    public CookedAssetMetadata? CookedSource { get; init; }
}
