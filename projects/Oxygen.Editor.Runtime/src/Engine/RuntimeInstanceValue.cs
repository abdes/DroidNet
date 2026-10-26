// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;
using Oxygen.Editor.World.Serialization;

namespace Oxygen.Editor.Runtime.Engine;

/// <summary>The value of an instance change, or of an instanced node's property, in the scene schema's units.</summary>
/// <remarks>An enum is its member's wire value; a material is the engine path of its cooked asset.</remarks>
[System.Diagnostics.CodeAnalysis.SuppressMessage("Design", "CA1034:Nested types should not be visible", Justification = "The closed set of value kinds belongs with its base.")]
public abstract record RuntimeInstanceValue
{
    private RuntimeInstanceValue()
    {
    }

    /// <summary>A bool value.</summary>
    /// <param name="Value">The value.</param>
    public sealed record Flag(bool Value) : RuntimeInstanceValue;

    /// <summary>An int value, or an enum member's wire value.</summary>
    /// <param name="Value">The value.</param>
    public sealed record Integer(int Value) : RuntimeInstanceValue;

    /// <summary>A float value.</summary>
    /// <param name="Value">The value.</param>
    public sealed record Scalar(float Value) : RuntimeInstanceValue;

    /// <summary>The three floats of a vector value.</summary>
    /// <param name="Value">The value.</param>
    public sealed record Vector(Vector3 Value) : RuntimeInstanceValue;

    /// <summary>An asset value.</summary>
    /// <param name="Path">The engine path of the cooked asset.</param>
    public sealed record Asset(string Path) : RuntimeInstanceValue;

    /// <summary>A LOD policy value.</summary>
    /// <param name="Policy">The policy.</param>
    public sealed record LodPolicyValue(LodPolicy Policy) : RuntimeInstanceValue;
}
