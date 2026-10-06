// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.World.Serialization;

/// <summary>Authored asset references owned by a scene.</summary>
public sealed record SceneReferencesData
{
    /// <summary>Gets the scene's script asset references.</summary>
    public IList<Uri> Scripts { get; init; } = [];

    /// <summary>Gets the scene's input action references.</summary>
    public IList<Uri> InputActions { get; init; } = [];

    /// <summary>Gets the scene's input mapping context references.</summary>
    public IList<Uri> InputMappingContexts { get; init; } = [];

    /// <summary>Gets the scene's physics sidecar references.</summary>
    public IList<Uri> PhysicsSidecars { get; init; } = [];

    /// <summary>Gets additional native virtual paths required by the scene.</summary>
    public IList<string> ExtraAssets { get; init; } = [];

    /// <summary>Gets a value indicating whether the scene has no authored references.</summary>
    [System.Text.Json.Serialization.JsonIgnore]
    public bool IsEmpty => this.Scripts.Count == 0
        && this.InputActions.Count == 0
        && this.InputMappingContexts.Count == 0
        && this.PhysicsSidecars.Count == 0
        && this.ExtraAssets.Count == 0;
}
