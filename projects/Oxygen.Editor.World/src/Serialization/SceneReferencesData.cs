// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Managed.Core;

namespace Oxygen.Editor.World.Serialization;

/// <summary>Authored asset references owned by a scene.</summary>
public sealed record SceneReferencesData
{
    /// <summary>Gets or sets the scene's script asset references.</summary>
    public IList<AssetPath> Scripts { get; set; } = [];

    /// <summary>Gets or sets the scene's input action references.</summary>
    public IList<AssetPath> InputActions { get; set; } = [];

    /// <summary>Gets or sets the scene's physics sidecar references.</summary>
    public IList<AssetPath> PhysicsSidecars { get; set; } = [];

    /// <summary>Gets or sets additional assets the scene requires, which its cooked descriptor lists by virtual path.</summary>
    public IList<AssetPath> ExtraAssets { get; set; } = [];

    /// <summary>Gets a value indicating whether the scene has no authored references.</summary>
    [System.Text.Json.Serialization.JsonIgnore]
    public bool IsEmpty => this.Scripts.Count == 0
        && this.InputActions.Count == 0
        && this.PhysicsSidecars.Count == 0
        && this.ExtraAssets.Count == 0;
}
