// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using System.Diagnostics.CodeAnalysis;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.World.Components;

/// <summary>
/// The values a scene stores over the sources of one component, such as the material of a geometry surface over the
/// geometry asset's: an absent value is always the source's.
/// </summary>
/// <remarks>
/// <para>
/// The value is immutable: every change returns a new one, so a component replaces it as a whole and an edit's inverse
/// is the previous value. Only a property <see cref="SceneSchema.Overridable"/> lists may be overridden, on its component
/// kind, with a value of its type; a part property needs a part of its part kind and the layout revision the part was
/// witnessed in. A part left without values is removed.
/// </para>
/// <para>
/// The changes a scene makes of one component of a node an instance brings in are the same store (<see
/// cref="ChangesOf"/>), over the instanced node's values: any property <see cref="SceneSchema.Changeable"/> lists may be
/// changed.
/// </para>
/// <para>
/// Overrides of a part the current asset no longer has are kept, unresolved, until they are removed.
/// </para>
/// </remarks>
public sealed class SourceOverrides : IEquatable<SourceOverrides>
{
    private readonly IReadOnlyDictionary<PropertyId, Type> valueTypes;

    private SourceOverrides(
        string componentKind,
        IReadOnlyDictionary<PropertyId, Type> valueTypes,
        ImmutableDictionary<PropertyId, object> values,
        ImmutableDictionary<PartId, PartOverrides> parts)
    {
        this.ComponentKind = componentKind;
        this.valueTypes = valueTypes;
        this.Values = values;
        this.Parts = parts;
    }

    /// <summary>
    /// Gets the kind of the component whose properties are overridden.
    /// </summary>
    public string ComponentKind { get; }

    /// <summary>
    /// Gets the overrides of the component's own properties.
    /// </summary>
    public ImmutableDictionary<PropertyId, object> Values { get; }

    /// <summary>
    /// Gets the overrides of each part that has any.
    /// </summary>
    public ImmutableDictionary<PartId, PartOverrides> Parts { get; }

    /// <summary>
    /// Gets a value indicating whether these are the changes of an instanced node (<see cref="ChangesOf"/>) rather than a
    /// source's overrides (<see cref="For"/>).
    /// </summary>
    public bool IsInstanceChanges => ReferenceEquals(this.valueTypes, SceneSchema.Changeable);

    /// <summary>
    /// Gets a value indicating whether no value is overridden.
    /// </summary>
    public bool IsEmpty => this.Values.IsEmpty && this.Parts.IsEmpty;

    /// <summary>
    /// Gets every override: the component's own, then each part's, ordered by part and property.
    /// </summary>
    public IEnumerable<SourceOverride> All
        => this.Values.OrderBy(static entry => entry.Key.Qualified(), StringComparer.Ordinal)
            .Select(static entry => new SourceOverride(entry.Key, Part: null, LayoutRevision: null, entry.Value))
            .Concat(this.Parts
                .OrderBy(static part => part.Key.Kind, StringComparer.Ordinal)
                .ThenBy(static part => part.Key.Id)
                .SelectMany(static part => part.Value.Values
                    .OrderBy(static entry => entry.Key.Qualified(), StringComparer.Ordinal)
                    .Select(entry => new SourceOverride(entry.Key, part.Key, part.Value.LayoutRevision, entry.Value))));

    /// <summary>
    /// Gets every asset an override references.
    /// </summary>
    public IEnumerable<AssetPath> Assets => this.All.Select(static entry => entry.Value).OfType<AssetPath>();

    /// <summary>
    /// Creates the overrides of a component that overrides nothing.
    /// </summary>
    /// <param name="componentKind">The component kind.</param>
    /// <returns>The empty overrides.</returns>
    public static SourceOverrides For(string componentKind)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(componentKind);
        return new(componentKind, SceneSchema.Overridable, [], []);
    }

    /// <summary>
    /// Creates the changes of one component of an instanced node that change nothing.
    /// </summary>
    /// <param name="componentKind">The component kind.</param>
    /// <returns>The empty changes.</returns>
    public static SourceOverrides ChangesOf(string componentKind)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(componentKind);
        return new(componentKind, SceneSchema.Changeable, [], []);
    }

    /// <summary>
    /// Tests whether a text is a layout revision: a lowercase hexadecimal SHA-256 digest.
    /// </summary>
    /// <param name="revision">The text.</param>
    /// <returns>Whether it is a layout revision.</returns>
    public static bool IsLayoutRevision([NotNullWhen(true)] string? revision)
        => revision is { Length: 64 } && revision.All(static digit => char.IsAsciiDigit(digit) || char.IsAsciiHexDigitLower(digit));

    /// <summary>
    /// Gets the value type of a property these overrides may hold.
    /// </summary>
    /// <param name="property">The property.</param>
    /// <param name="type">The value type.</param>
    /// <returns>Whether the property may be overridden here.</returns>
    public bool TryGetValueType(PropertyId property, [NotNullWhen(true)] out Type? type)
    {
        ArgumentNullException.ThrowIfNull(property);
        return this.valueTypes.TryGetValue(property, out type)
            && string.Equals(property.ComponentKind, this.ComponentKind, StringComparison.Ordinal);
    }

    /// <summary>
    /// Gets the override of a property of the component itself.
    /// </summary>
    /// <typeparam name="T">The property's value type.</typeparam>
    /// <param name="property">The property.</param>
    /// <param name="value">The override.</param>
    /// <returns>Whether the property is overridden.</returns>
    public bool TryGet<T>(PropertyId<T> property, [MaybeNullWhen(false)] out T value)
        where T : notnull
    {
        ArgumentNullException.ThrowIfNull(property);
        return Cast(this.Values.GetValueOrDefault(property.Id), out value);
    }

    /// <summary>
    /// Gets the override of a property of a part.
    /// </summary>
    /// <typeparam name="T">The property's value type.</typeparam>
    /// <param name="property">The property.</param>
    /// <param name="part">The part.</param>
    /// <param name="value">The override.</param>
    /// <returns>Whether the property is overridden on that part.</returns>
    public bool TryGet<T>(PropertyId<T> property, PartId part, [MaybeNullWhen(false)] out T value)
        where T : notnull
    {
        ArgumentNullException.ThrowIfNull(property);
        return Cast(this.Parts.GetValueOrDefault(part)?.Values.GetValueOrDefault(property.Id), out value);
    }

    /// <summary>
    /// Finds the override of a property.
    /// </summary>
    /// <param name="property">The property.</param>
    /// <param name="part">The part, or <see langword="null"/> for a property of the component itself.</param>
    /// <returns>The override, with its part's layout revision, or <see langword="null"/> when the property is not overridden.</returns>
    public SourceOverride? Find(PropertyId property, PartId? part)
    {
        ArgumentNullException.ThrowIfNull(property);
        if (part is not { } id)
        {
            return this.Values.TryGetValue(property, out var value) ? new SourceOverride(property, Part: null, LayoutRevision: null, value) : null;
        }

        return this.Parts.GetValueOrDefault(id) is { } overrides && overrides.Values.TryGetValue(property, out var partValue)
            ? new SourceOverride(property, id, overrides.LayoutRevision, partValue)
            : null;
    }

    /// <summary>
    /// Overrides a property of the component itself.
    /// </summary>
    /// <typeparam name="T">The property's value type.</typeparam>
    /// <param name="property">The property.</param>
    /// <param name="value">The value.</param>
    /// <returns>The overrides with the value.</returns>
    /// <exception cref="ArgumentException">The property cannot be overridden on this component, or has a part kind.</exception>
    public SourceOverrides With<T>(PropertyId<T> property, T value)
        where T : notnull
    {
        ArgumentNullException.ThrowIfNull(property);
        return this.With(new SourceOverride(property.Id, Part: null, LayoutRevision: null, value));
    }

    /// <summary>
    /// Overrides a property of a part.
    /// </summary>
    /// <typeparam name="T">The property's value type.</typeparam>
    /// <param name="property">The property.</param>
    /// <param name="part">The part.</param>
    /// <param name="layoutRevision">The revision of the part layout the part was witnessed in; it becomes the part's.</param>
    /// <param name="value">The value.</param>
    /// <returns>The overrides with the value.</returns>
    /// <exception cref="ArgumentException">The property cannot be overridden on this component or this part, or the revision is not one.</exception>
    public SourceOverrides With<T>(PropertyId<T> property, PartId part, string layoutRevision, T value)
        where T : notnull
    {
        ArgumentNullException.ThrowIfNull(property);
        return this.With(new SourceOverride(property.Id, part, layoutRevision, value));
    }

    /// <summary>
    /// Sets one override, replacing the property's previous one; a part override also sets the part's layout revision.
    /// </summary>
    /// <param name="entry">The override.</param>
    /// <returns>The overrides with the value.</returns>
    /// <exception cref="ArgumentException">The override does not fit the schema: see <see cref="SourceOverrides"/>.</exception>
    public SourceOverrides With(SourceOverride entry)
    {
        this.Check(entry);
        if (entry.Part is not { } part)
        {
            return new(this.ComponentKind, this.valueTypes, this.Values.SetItem(entry.Property, entry.Value), this.Parts);
        }

        var values = this.Parts.GetValueOrDefault(part)?.Values ?? [];
        var updated = new PartOverrides(entry.LayoutRevision!, values.SetItem(entry.Property, entry.Value));
        return new(this.ComponentKind, this.valueTypes, this.Values, this.Parts.SetItem(part, updated));
    }

    /// <summary>
    /// Removes the override of a property, so the source's value applies again.
    /// </summary>
    /// <param name="property">The property.</param>
    /// <param name="part">The part, or <see langword="null"/> for a property of the component itself.</param>
    /// <returns>The overrides without that override; the same value when there was none.</returns>
    public SourceOverrides Without(PropertyId property, PartId? part = null)
    {
        ArgumentNullException.ThrowIfNull(property);
        if (part is not { } id)
        {
            return this.Values.ContainsKey(property) ? new(this.ComponentKind, this.valueTypes, this.Values.Remove(property), this.Parts) : this;
        }

        if (this.Parts.GetValueOrDefault(id) is not { } overrides || !overrides.Values.ContainsKey(property))
        {
            return this;
        }

        var values = overrides.Values.Remove(property);
        return new(this.ComponentKind, this.valueTypes, this.Values, values.IsEmpty ? this.Parts.Remove(id) : this.Parts.SetItem(id, overrides with { Values = values }));
    }

    /// <summary>
    /// Replaces every asset an override references.
    /// </summary>
    /// <param name="map">Returns the asset that replaces one, or the same asset.</param>
    /// <returns>The overrides with the replaced assets.</returns>
    public SourceOverrides MapAssets(Func<AssetPath, AssetPath> map)
    {
        ArgumentNullException.ThrowIfNull(map);
        var result = this;
        foreach (var entry in this.All.Where(static entry => entry.Value is AssetPath))
        {
            var asset = map((AssetPath)entry.Value);
            result = asset == (AssetPath)entry.Value ? result : result.With(entry with { Value = asset });
        }

        return result;
    }

    /// <inheritdoc/>
    public bool Equals(SourceOverrides? other)
        => other is not null
            && string.Equals(this.ComponentKind, other.ComponentKind, StringComparison.Ordinal)
            && ReferenceEquals(this.valueTypes, other.valueTypes)
            && this.All.SequenceEqual(other.All);

    /// <inheritdoc/>
    public override bool Equals(object? obj) => this.Equals(obj as SourceOverrides);

    /// <inheritdoc/>
    public override int GetHashCode() => HashCode.Combine(this.ComponentKind, this.Values.Count, this.Parts.Count);

    private static bool Cast<T>(object? stored, [MaybeNullWhen(false)] out T value)
    {
        if (stored is T typed)
        {
            value = typed;
            return true;
        }

        value = default;
        return false;
    }

    private void Check(SourceOverride entry)
    {
        ArgumentNullException.ThrowIfNull(entry.Property);
        var property = entry.Property;
        if (!string.Equals(property.ComponentKind, this.ComponentKind, StringComparison.Ordinal))
        {
            throw new ArgumentException($"{property} is not a property of a {this.ComponentKind} component.", nameof(entry));
        }

        if (!this.valueTypes.TryGetValue(property, out var type))
        {
            throw new ArgumentException(
                this.IsInstanceChanges
                    ? $"{property} cannot be changed on an instanced node."
                    : $"{property} has no source, so it cannot be overridden.",
                nameof(entry));
        }

        if (!type.IsInstanceOfType(entry.Value))
        {
            throw new ArgumentException($"{property} takes a {type.Name}, not {entry.Value?.GetType().Name ?? "null"}.", nameof(entry));
        }

        if (property.PartKind is null)
        {
            if (entry.Part is not null || entry.LayoutRevision is not null)
            {
                throw new ArgumentException($"{property} is a property of the component, not of a part.", nameof(entry));
            }

            return;
        }

        if (entry.Part is not { } part || !string.Equals(part.Kind, property.PartKind, StringComparison.Ordinal) || part.Id == Guid.Empty)
        {
            throw new ArgumentException($"{property} needs a {property.PartKind} part.", nameof(entry));
        }

        if (!IsLayoutRevision(entry.LayoutRevision))
        {
            throw new ArgumentException($"'{entry.LayoutRevision}' is not a layout revision.", nameof(entry));
        }
    }
}
