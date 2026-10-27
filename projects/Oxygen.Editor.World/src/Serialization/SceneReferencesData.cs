// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.World.Serialization;

/// <summary>Authored asset references owned by a scene.</summary>
public sealed record SceneReferencesData
{
    /// <summary>Gets or sets the scene's script asset references.</summary>
    [SuppressMessage("Usage", "CA2227:Collection properties should be read only", Justification = "System.Text.Json source generation sets an absent init-only property to null, losing the empty default; the setter keeps it (SceneJsonDefaultsTests).")]
    public IList<AssetPath> Scripts { get; set; } = [];

    /// <summary>Gets or sets the scene's input action references.</summary>
    [SuppressMessage("Usage", "CA2227:Collection properties should be read only", Justification = "System.Text.Json source generation sets an absent init-only property to null, losing the empty default; the setter keeps it (SceneJsonDefaultsTests).")]
    public IList<AssetPath> InputActions { get; set; } = [];

    /// <summary>Gets or sets the scene's physics sidecar references.</summary>
    [SuppressMessage("Usage", "CA2227:Collection properties should be read only", Justification = "System.Text.Json source generation sets an absent init-only property to null, losing the empty default; the setter keeps it (SceneJsonDefaultsTests).")]
    public IList<AssetPath> PhysicsSidecars { get; set; } = [];

    /// <summary>Gets or sets additional assets the scene requires, which its cooked descriptor lists by virtual path.</summary>
    [SuppressMessage("Usage", "CA2227:Collection properties should be read only", Justification = "System.Text.Json source generation sets an absent init-only property to null, losing the empty default; the setter keeps it (SceneJsonDefaultsTests).")]
    public IList<AssetPath> ExtraAssets { get; set; } = [];

    /// <summary>Gets a value indicating whether the scene has no authored references.</summary>
    [System.Text.Json.Serialization.JsonIgnore]
    public bool IsEmpty => this.Scripts.Count == 0
        && this.InputActions.Count == 0
        && this.PhysicsSidecars.Count == 0
        && this.ExtraAssets.Count == 0;
}
