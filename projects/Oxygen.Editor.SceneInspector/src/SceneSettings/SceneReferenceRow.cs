// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Assets;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.SceneInspector.SceneSettings;

/// <summary>Displays a typed scene reference and whether it is currently resolvable.</summary>
public sealed record SceneReferenceRow(AssetKind Kind, AssetPath Asset, string DisplayName, string DisplayPath, bool IsUnavailable, bool CanEdit)
{
    /// <summary>Gets searchable display text for this reference.</summary>
    public string SearchText => this.DisplayName + " " + this.DisplayPath;

    /// <summary>Gets the current resolution status.</summary>
    public string StatusText => this.IsUnavailable ? "Unavailable or wrong asset type" : "Resolved";
}
