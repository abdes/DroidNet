// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.World.Serialization;

namespace Oxygen.Editor.Runtime.Engine;

/// <summary>The value of an override, one kind per value type of the scene schema.</summary>
[System.Diagnostics.CodeAnalysis.SuppressMessage("Design", "CA1034:Nested types should not be visible", Justification = "The closed set of value kinds belongs with its base.")]
public abstract record RuntimeOverrideValue
{
    private RuntimeOverrideValue()
    {
    }

    /// <summary>An asset value: the cooked engine path of the asset.</summary>
    /// <param name="Path">The engine virtual path.</param>
    public sealed record Asset(string Path) : RuntimeOverrideValue;

    /// <summary>A bool value.</summary>
    /// <param name="Value">The value.</param>
    public sealed record Flag(bool Value) : RuntimeOverrideValue;

    /// <summary>A LOD policy value.</summary>
    /// <param name="Policy">The policy.</param>
    public sealed record LodPolicyValue(LodPolicy Policy) : RuntimeOverrideValue;
}
