// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.Scenes.Edits.Properties;
using Oxygen.Editor.Schemas;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Scenes.Tests.TestSupport;

/// <summary>
/// One row of a property rule test: set one property of one node, or of the scene, to one value.
/// </summary>
/// <remarks>
/// Rows are made with the <c>Of</c> overloads, so a value always has its property's exact type (a <see cref="float"/>
/// row cannot carry a <see cref="double"/>).
/// </remarks>
public sealed record PropertyCase
{
    private PropertyCase(string name, NodeId? node, PropertyId property, object? value)
    {
        this.Name = name;
        this.Node = node;
        this.Property = property;
        this.Value = value;
    }

    /// <summary>Gets the row's display name, such as <c>Range_Zero</c>.</summary>
    public string Name { get; }

    /// <summary>Gets the node whose property is set, or <see langword="null"/> for a scene property.</summary>
    public NodeId? Node { get; }

    /// <summary>Gets the property.</summary>
    public PropertyId Property { get; }

    /// <summary>Gets the value, boxed from the property's own type.</summary>
    public object? Value { get; }

    /// <summary>
    /// Makes a row.
    /// </summary>
    /// <typeparam name="T">The property's type.</typeparam>
    /// <param name="name">The row's display name.</param>
    /// <param name="node">The node.</param>
    /// <param name="property">The property.</param>
    /// <param name="value">The value.</param>
    /// <returns>The row, ready for <c>[DynamicData]</c>.</returns>
    public static TestDataRow<PropertyCase> Of<T>(string name, NodeId node, PropertyId<T> property, T value)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(name);
        ArgumentNullException.ThrowIfNull(property);
        return new TestDataRow<PropertyCase>(new PropertyCase(name, node, property.Id, value)) { DisplayName = name };
    }

    /// <summary>
    /// Makes a row for a property known by its descriptor.
    /// </summary>
    /// <typeparam name="T">The property's type.</typeparam>
    /// <param name="name">The row's display name.</param>
    /// <param name="node">The node.</param>
    /// <param name="property">The property's descriptor; nullable because some exist for only some kinds.</param>
    /// <param name="value">The value.</param>
    /// <returns>The row, ready for <c>[DynamicData]</c>.</returns>
    /// <exception cref="ArgumentException">The descriptor is <see langword="null"/>.</exception>
    public static TestDataRow<PropertyCase> Of<T>(string name, NodeId node, PropertyDescriptor<T>? property, T value)
    {
        // Some descriptors exist for only some kinds, such as the cones of a spot light.
        var descriptor = property ?? throw new ArgumentException("The descriptor does not exist for this kind.", nameof(property));
        return Of(name, node, new PropertyId<T>(descriptor.Id), value);
    }

    /// <summary>
    /// Makes a row for a scene property.
    /// </summary>
    /// <typeparam name="T">The property's type.</typeparam>
    /// <param name="name">The row's display name.</param>
    /// <param name="property">The property.</param>
    /// <param name="value">The value.</param>
    /// <returns>The row, ready for <c>[DynamicData]</c>.</returns>
    public static TestDataRow<PropertyCase> OfScene<T>(string name, PropertyId<T> property, T value)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(name);
        ArgumentNullException.ThrowIfNull(property);
        return new TestDataRow<PropertyCase>(new PropertyCase(name, node: null, property.Id, value)) { DisplayName = name };
    }

    /// <summary>
    /// Makes the edit that sets the row's property: <see cref="SetProperties"/> on the node, or
    /// <see cref="SetSceneProperties"/> for a scene property.
    /// </summary>
    /// <returns>The edit.</returns>
    public ISceneEdit Edit()
    {
        var values = new PropertyEdit.Builder().SetRaw(this.Property, this.Value).Build();
        return this.Node is { } node
            ? new SetProperties(new Dictionary<NodeId, PropertyEdit> { [node] = values }, this.Name)
            : new SetSceneProperties(values, this.Name);
    }

    /// <inheritdoc/>
    public override string ToString() => this.Name;
}
