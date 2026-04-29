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
[DebuggerDisplay("{ComponentKind,nq}{Pointer,nq}")]
public sealed record PropertyId(string ComponentKind, string Pointer)
{
    /// <summary>
    /// Builds a fully-qualified key suitable for hash-table lookup.
    /// </summary>
    /// <returns>The qualified key, formatted as <c>component#pointer</c>.</returns>
    public string Qualified() => $"{this.ComponentKind}#{this.Pointer}";

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
public sealed record PropertyId<T>(PropertyId Id)
{
    /// <summary>
    /// Convenience constructor that builds the underlying <see cref="PropertyId"/>
    /// from a component kind and JSON Pointer.
    /// </summary>
    /// <param name="componentKind">The component kind (e.g. <c>transform</c>).</param>
    /// <param name="pointer">The JSON Pointer of the field within the component schema.</param>
    public PropertyId(string componentKind, string pointer)
        : this(new PropertyId(componentKind, pointer))
    {
    }

    /// <inheritdoc />
    public override string ToString() => $"{this.Id} : {typeof(T).Name}";
}
