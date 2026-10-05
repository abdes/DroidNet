// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Schemas;

/// <summary>
/// Untyped property descriptor base; participates in registries that need
/// to enumerate descriptors regardless of their value type.
/// </summary>
public abstract class PropertyDescriptor
{
    /// <summary>
    /// Initializes a new instance of the <see cref="PropertyDescriptor"/> class.
    /// </summary>
    /// <param name="id">The untyped identity.</param>
    /// <param name="valueType">The value type.</param>
    /// <param name="annotation">The parsed editor annotations.</param>
    /// <param name="engineCommandKey">The engine-side property key for the dispatch table.</param>
    protected PropertyDescriptor(
        PropertyId id,
        Type valueType,
        EditorAnnotation annotation,
        string engineCommandKey)
    {
        ArgumentNullException.ThrowIfNull(id);
        ArgumentNullException.ThrowIfNull(valueType);
        ArgumentNullException.ThrowIfNull(annotation);
        ArgumentException.ThrowIfNullOrWhiteSpace(engineCommandKey);

        this.Id = id;
        this.ValueType = valueType;
        this.Annotation = annotation;
        this.EngineCommandKey = engineCommandKey;
    }

    /// <summary>
    /// Gets the untyped identity.
    /// </summary>
    public PropertyId Id { get; }

    /// <summary>
    /// Gets the C# value type bound by the descriptor.
    /// </summary>
    public Type ValueType { get; }

    /// <summary>
    /// Gets the parsed editor annotations.
    /// </summary>
    public EditorAnnotation Annotation { get; }

    /// <summary>
    /// Gets the engine-side property key consumed by the property
    /// dispatch table on the C++ side. Stable across versions.
    /// </summary>
    public string EngineCommandKey { get; }

    /// <summary>
    /// Reads the current value from a target object as a boxed value.
    /// </summary>
    /// <param name="target">The component / model object.</param>
    /// <returns>The value, boxed.</returns>
    public abstract object? ReadBoxed(object target);

    /// <summary>
    /// Writes a boxed value into a target object.
    /// </summary>
    /// <param name="target">The component / model object.</param>
    /// <param name="value">The new value, boxed.</param>
    public abstract void WriteBoxed(object target, object? value);

    /// <summary>
    /// Validates a boxed candidate value.
    /// </summary>
    /// <param name="value">The candidate value, boxed.</param>
    /// <returns>The validation result.</returns>
    public abstract ValidationResult ValidateBoxed(object? value);
}
