// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;

namespace Oxygen.Editor.Schemas;

/// <summary>
/// Untyped property identity. Stable across editor sessions.
/// </summary>
/// <remarks>
/// A property id is the JSON Pointer of the field within its component
/// schema. For example, the X axis of a transform's local position is
/// <c>/local_position/0</c>; the metalness scalar of a material is
/// <c>/parameters/metalness</c>.
/// <para>
/// Pointers are kept as opaque strings; the schema layer is responsible
/// for resolving them against the merged engine + overlay schema.
/// </para>
/// </remarks>
[DebuggerDisplay("{ComponentKind,nq}{JsonPointer,nq}")]
public sealed record PropertyId(string ComponentKind, string JsonPointer)
{
    /// <summary>
    /// Builds a fully-qualified key suitable for hash-table lookup.
    /// </summary>
    /// <returns>The qualified key, formatted as <c>component#jsonPointer</c>.</returns>
    public string Qualified() => $"{this.ComponentKind}#{this.JsonPointer}";

    /// <inheritdoc />
    public override string ToString() => this.Qualified();
}

/// <summary>
/// Typed property identity. The type parameter is the C# value type the
/// editor uses to represent the property at the binding boundary.
/// </summary>
/// <typeparam name="T">The bound value type (e.g. <see cref="float"/>).</typeparam>
/// <param name="Id">The untyped identity.</param>
[DebuggerDisplay("{Id,nq} : {typeof(T).Name,nq}")]
[System.Diagnostics.CodeAnalysis.SuppressMessage("StyleCop.CSharp.MaintainabilityRules", "SA1402:File may only contain a single type", Justification = "generic and non-generic in same file")]
public sealed record PropertyId<T>(PropertyId Id)
{
    /// <summary>
    /// Initializes a new instance of the <see cref="PropertyId{T}"/> class.
    /// Convenience constructor that builds the underlying <see cref="PropertyId"/>
    /// from a component kind and JSON Pointer.
    /// </summary>
    /// <param name="componentKind">The component kind (e.g. <c>transform</c>).</param>
    /// <param name="jsonPointer">The JSON Pointer of the field within the component schema.</param>
    public PropertyId(string componentKind, string jsonPointer)
        : this(new PropertyId(componentKind, jsonPointer))
    {
    }

    /// <inheritdoc />
    public override string ToString() => $"{this.Id} : {typeof(T).Name}";
}
