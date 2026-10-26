// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using System.Globalization;
using System.Text.Json;
using System.Text.Json.Nodes;

namespace Oxygen.Editor.Models;

/// <summary>One model import setting: a key of a <c>gltf</c> or <c>fbx</c> import manifest job, which a model import
/// record's settings and an import session's job hold alike.</summary>
/// <remarks>
/// A setting the settings do not hold takes its <see cref="Default"/>, the importer's own, so an editor import and an
/// ImportTool import of the same settings agree. Values are <see cref="string"/> for <see cref="ModelImportSettingKind.Choice"/>
/// and <see cref="ModelImportSettingKind.Text"/>, <see cref="double"/> for <see cref="ModelImportSettingKind.Number"/>,
/// <see cref="int"/> for <see cref="ModelImportSettingKind.Integer"/> and <see cref="bool"/> for
/// <see cref="ModelImportSettingKind.Toggle"/>.
/// </remarks>
public sealed record ModelImportSetting
{
    /// <summary>Gets the job key, such as <c>unit_policy</c>.</summary>
    public required string Key { get; init; }

    /// <summary>Gets the kind of value the key holds.</summary>
    public required ModelImportSettingKind Kind { get; init; }

    /// <summary>Gets the importer's value when the settings do not hold the key, or <see langword="null"/> when the
    /// importer then decides from the file (the source size of a texture, the file's default scene).</summary>
    public object? Default { get; init; }

    /// <summary>Gets every value a choice may hold, as the manifest schema lists them.</summary>
    public ImmutableArray<string> Choices { get; init; } = [];

    /// <summary>Gets the smallest value a number may hold, if any.</summary>
    public double? Minimum { get; init; }

    /// <summary>Gets a value indicating whether <see cref="Minimum"/> itself is refused.</summary>
    public bool IsMinimumExclusive { get; init; }

    /// <summary>Gets the largest value a number may hold, if any.</summary>
    public double? Maximum { get; init; }

    /// <summary>Gets the formats whose importer reads the key; another format reports it as ignored.</summary>
    public ModelSourceFormats Formats { get; init; } = ModelSourceFormats.All;

    /// <summary>Gets the value the settings hold for this key.</summary>
    /// <param name="settings">Model import settings, a JSON object.</param>
    /// <returns>The value, or <see langword="null"/> when the settings do not hold the key.</returns>
    /// <exception cref="InvalidDataException">The settings hold a value of another kind.</exception>
    public object? ReadFrom(JsonElement settings)
    {
        if (settings.ValueKind != JsonValueKind.Object || !settings.TryGetProperty(this.Key, out var value))
        {
            return null;
        }

        return (this.Kind, value.ValueKind) switch
        {
            (ModelImportSettingKind.Choice or ModelImportSettingKind.Text, JsonValueKind.String) => value.GetString(),
            (ModelImportSettingKind.Number, JsonValueKind.Number) => value.GetDouble(),
            (ModelImportSettingKind.Integer, JsonValueKind.Number) when value.TryGetInt32(out var number) => number,
            (ModelImportSettingKind.Toggle, JsonValueKind.True or JsonValueKind.False) => value.GetBoolean(),
            _ => throw new InvalidDataException($"The import setting '{this.Key}' holds a value of another kind."),
        };
    }

    /// <summary>Gets the settings with this key set to <paramref name="value"/>, or removed for <see langword="null"/>;
    /// every other key is kept as it is.</summary>
    /// <param name="settings">Model import settings, a JSON object.</param>
    /// <param name="value">The value, of this setting's kind, or <see langword="null"/> to remove the key.</param>
    /// <returns>The changed settings.</returns>
    /// <exception cref="ArgumentException">The value is not of this setting's kind or is out of its range.</exception>
    public JsonElement WriteTo(JsonElement settings, object? value)
    {
        var copy = settings.ValueKind == JsonValueKind.Object ? JsonNode.Parse(settings.GetRawText())!.AsObject() : [];
        if (value is null)
        {
            _ = copy.Remove(this.Key);
        }
        else
        {
            copy[this.Key] = this.ToNode(value);
        }

        return JsonSerializer.SerializeToElement(copy);
    }

    /// <summary>Gets whether <paramref name="value"/> is one this setting accepts.</summary>
    /// <param name="value">A value of this setting's kind.</param>
    /// <returns><see langword="true"/> when the value is of the setting's kind, one of its choices and in its range.</returns>
    public bool Accepts(object? value)
        => (this.Kind, value) switch
        {
            (ModelImportSettingKind.Choice, string choice) => this.Choices.Contains(choice, StringComparer.Ordinal),
            (ModelImportSettingKind.Text, string text) => text.Length != 0,
            (ModelImportSettingKind.Number, double number) => this.InRange(number),
            (ModelImportSettingKind.Integer, int number) => this.InRange(number),
            (ModelImportSettingKind.Toggle, bool) => true,
            _ => false,
        };

    private bool InRange(double value)
        => double.IsFinite(value)
            && (this.Minimum is not { } minimum || (this.IsMinimumExclusive ? value > minimum : value >= minimum))
            && (this.Maximum is not { } maximum || value <= maximum);

    private JsonNode ToNode(object value)
    {
        if (!this.Accepts(value))
        {
            throw new ArgumentException(
                string.Create(CultureInfo.InvariantCulture, $"'{value}' is not a value of the import setting '{this.Key}'."),
                nameof(value));
        }

        return value switch
        {
            string text => JsonValue.Create(text),
            double number => JsonValue.Create(number),
            int number => JsonValue.Create(number),
            bool flag => JsonValue.Create(flag),
            _ => throw new ArgumentException($"The import setting '{this.Key}' holds no value of this type.", nameof(value)),
        };
    }
}
