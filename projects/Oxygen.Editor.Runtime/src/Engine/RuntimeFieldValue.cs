// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Runtime.Engine;

/// <summary>The value of an engine field, one kind per field value type of the scene schema.</summary>
/// <remarks>
/// The schema gives every field one value type, and the engine refuses a value of another: a bool property goes as a
/// <see cref="Flag"/>, an int property or an enum member's wire value as an <see cref="Integer"/>, and a float property
/// or one axis of a vector as a <see cref="Scalar"/>.
/// </remarks>
[System.Diagnostics.CodeAnalysis.SuppressMessage("Design", "CA1034:Nested types should not be visible", Justification = "The closed set of value kinds belongs with its base.")]
public abstract record RuntimeFieldValue
{
    private RuntimeFieldValue()
    {
    }

    /// <summary>A bool field value.</summary>
    /// <param name="Value">The value.</param>
    public sealed record Flag(bool Value) : RuntimeFieldValue;

    /// <summary>An int32 field value: an int, or the wire value of an enum member.</summary>
    /// <param name="Value">The value.</param>
    public sealed record Integer(int Value) : RuntimeFieldValue;

    /// <summary>A float field value, in engine units.</summary>
    /// <param name="Value">The value.</param>
    public sealed record Scalar(float Value) : RuntimeFieldValue;
}
