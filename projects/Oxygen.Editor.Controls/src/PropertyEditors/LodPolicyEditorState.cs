// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.World.Serialization;

namespace Oxygen.Editor.Controls.PropertyEditors;

/// <summary>What the Level of detail editor shows: the policy, the policy it inherits and how its owner presents them.</summary>
/// <param name="Policy">The policy in effect where it is edited, or <see langword="null"/> when the edited policies differ.</param>
/// <param name="Inherited">The policy that applies without one set here, such as the geometry asset's.</param>
/// <param name="LodCount">The geometry's number of LODs.</param>
public sealed record LodPolicyEditorState(LodPolicy? Policy, LodPolicy Inherited, int LodCount)
{
    /// <summary>Gets a value indicating whether the rows show.</summary>
    public bool IsAvailable { get; init; } = true;

    /// <summary>Gets a value indicating whether the policy can be edited now.</summary>
    public bool CanEdit { get; init; } = true;

    /// <summary>Gets whether the policy is set where it is edited.</summary>
    public OverrideState OverrideState { get; init; }

    /// <summary>Gets the reset's tooltip, which names the inherited policy.</summary>
    public string ResetToolTip { get; init; } = string.Empty;

    /// <summary>Gets the boundaries disclosure's summary, which names the policy's source.</summary>
    public string Summary { get; init; } = string.Empty;
}
