// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System;
using System.Collections.Generic;

namespace Oxygen.Editor.Schemas;

/// <summary>
/// The pure validation result for a candidate property value.
/// </summary>
/// <param name="IsValid">Whether the value satisfies all constraints.</param>
/// <param name="Code">A short, machine-readable error code (empty when valid).</param>
/// <param name="Message">A human-readable error message (empty when valid).</param>
public readonly record struct ValidationResult(bool IsValid, string Code, string Message)
{
    /// <summary>
    /// The canonical "ok" result.
    /// </summary>
    public static ValidationResult Ok { get; } = new(true, string.Empty, string.Empty);

    /// <summary>
    /// Builds a failure result.
    /// </summary>
    /// <param name="code">A machine-readable error code.</param>
    /// <param name="message">A human-readable message.</param>
    /// <returns>The failure result.</returns>
    public static ValidationResult Fail(string code, string message) => new(false, code, message);
}

/// <summary>
/// Editor-side metadata parsed from the <c>x-editor-*</c> annotation
/// namespace of an overlay schema.
/// </summary>
/// <remarks>
/// The fields here are nullable when "absent in the overlay" is a
/// distinct state from "explicitly empty"; the schema catalog populates
/// them from the merged engine + overlay tree.
/// </remarks>
public sealed record EditorAnnotation
{
    /// <summary>
    /// Gets or sets the human-readable label shown in the inspector.
    /// </summary>
    public string? Label { get; init; }

    /// <summary>
    /// Gets or sets the inspector group / category path (e.g.
    /// <c>"Parameters/Surface"</c>).
    /// </summary>
    public string? Group { get; init; }

    /// <summary>
    /// Gets or sets the ordering hint within the group.
    /// </summary>
    public int? Order { get; init; }

    /// <summary>
    /// Gets or sets the renderer key (e.g. <c>slider</c>, <c>numberbox</c>,
    /// <c>color-rgba</c>, <c>asset-picker</c>).
    /// </summary>
    public string? Renderer { get; init; }

    /// <summary>
    /// Gets or sets a help/tooltip string.
    /// </summary>
    public string? Tooltip { get; init; }

    /// <summary>
    /// Gets or sets the increment used by spinners / sliders.
    /// </summary>
    public double? Step { get; init; }

    /// <summary>
    /// Gets or sets a soft maximum that clamps the spinner range without
    /// forbidding hand-edited higher values. The engine schema's
    /// <c>maximum</c> is the only validation authority.
    /// </summary>
    public double? SoftMax { get; init; }

    /// <summary>
    /// Gets or sets a soft minimum that clamps the spinner range without
    /// forbidding hand-edited lower values.
    /// </summary>
    public double? SoftMin { get; init; }

    /// <summary>
    /// Gets or sets a flag that hides the field behind an "advanced"
    /// disclosure in the inspector.
    /// </summary>
    public bool Advanced { get; init; }

    /// <summary>
    /// Gets the unparsed extra annotations (renderer-specific keys,
    /// e.g. <c>x-editor-color-space</c>).
    /// </summary>
    public IReadOnlyDictionary<string, object?> Extra { get; init; } = new Dictionary<string, object?>(StringComparer.Ordinal);
}

/// <summary>
/// Untyped property descriptor base; participates in registries that need
/// to enumerate descriptors regardless of their value type.
/// </summary>
public abstract class PropertyDescriptor
{
    /// <summary>
    /// Initializes a new descriptor.
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

/// <summary>
/// Typed property descriptor.
/// </summary>
/// <typeparam name="T">The bound C# value type.</typeparam>
public sealed class PropertyDescriptor<T> : PropertyDescriptor
{
    /// <summary>
    /// Initializes a new typed descriptor.
    /// </summary>
    /// <param name="id">The typed identity.</param>
    /// <param name="reader">Reads the current value from a model object.</param>
    /// <param name="writer">Writes a value to the model object.</param>
    /// <param name="validator">Validates a candidate value.</param>
    /// <param name="annotation">The parsed editor annotations.</param>
    /// <param name="engineCommandKey">The engine-side property key.</param>
    public PropertyDescriptor(
        PropertyId<T> id,
        Func<object, T> reader,
        Action<object, T> writer,
        Func<T, ValidationResult> validator,
        EditorAnnotation annotation,
        string engineCommandKey)
        : base(id.Id, typeof(T), annotation, engineCommandKey)
    {
        ArgumentNullException.ThrowIfNull(reader);
        ArgumentNullException.ThrowIfNull(writer);
        ArgumentNullException.ThrowIfNull(validator);

        this.TypedId = id;
        this.Reader = reader;
        this.Writer = writer;
        this.Validator = validator;
    }

    /// <summary>
    /// Gets the typed identity.
    /// </summary>
    public PropertyId<T> TypedId { get; }

    /// <summary>
    /// Gets the reader function.
    /// </summary>
    public Func<object, T> Reader { get; }

    /// <summary>
    /// Gets the writer function.
    /// </summary>
    public Action<object, T> Writer { get; }

    /// <summary>
    /// Gets the validator function.
    /// </summary>
    public Func<T, ValidationResult> Validator { get; }

    /// <summary>
    /// Reads the typed value.
    /// </summary>
    /// <param name="target">The model object.</param>
    /// <returns>The current value.</returns>
    public T Read(object target) => this.Reader(target);

    /// <summary>
    /// Writes the typed value.
    /// </summary>
    /// <param name="target">The model object.</param>
    /// <param name="value">The new value.</param>
    public void Write(object target, T value) => this.Writer(target, value);

    /// <summary>
    /// Validates a typed candidate value.
    /// </summary>
    /// <param name="value">The candidate value.</param>
    /// <returns>The validation result.</returns>
    public ValidationResult Validate(T value) => this.Validator(value);

    /// <inheritdoc />
    public override object? ReadBoxed(object target) => this.Reader(target);

    /// <inheritdoc />
    public override void WriteBoxed(object target, object? value)
    {
        if (value is T typed)
        {
            this.Writer(target, typed);
            return;
        }

        if (value is null && default(T) is null)
        {
            this.Writer(target, default!);
            return;
        }

        throw new ArgumentException($"Property {this.Id} expects a {typeof(T).Name}; got {value?.GetType().Name ?? "null"}.", nameof(value));
    }

    /// <inheritdoc />
    public override ValidationResult ValidateBoxed(object? value)
    {
        if (value is T typed)
        {
            return this.Validator(typed);
        }

        if (value is null && default(T) is null)
        {
            return this.Validator(default!);
        }

        return ValidationResult.Fail(
            "PROPERTY_TYPE_MISMATCH",
            $"Property {this.Id} expects a {typeof(T).Name}; got {value?.GetType().Name ?? "null"}.");
    }
}
