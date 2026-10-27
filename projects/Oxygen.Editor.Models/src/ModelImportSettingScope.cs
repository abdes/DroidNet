// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json;
using System.Text.Json.Nodes;

namespace Oxygen.Editor.Models;

/// <summary>
/// The part of a model's import settings a setting is read from and written to: the model's own settings, or one
/// texture's overrides of the model's texture settings (<c>texture_overrides</c>, by the name the import gives the
/// texture).
/// </summary>
/// <remarks>
/// A texture override holds the texture settings of the import manifest (maximum size, compression quality, normal map
/// convention); a setting it does not hold takes the model's value, then the importer's default.
/// </remarks>
public abstract record ModelImportSettingScope
{
    private ModelImportSettingScope()
    {
    }

    /// <summary>Gets the scope of the model's own settings.</summary>
    public static ModelImportSettingScope Model { get; } = new ModelScope();

    /// <summary>Gets the scope of one texture's overrides.</summary>
    /// <param name="texture">The texture's name in the import.</param>
    /// <returns>The scope.</returns>
    public static ModelImportSettingScope Texture(string texture)
    {
        ArgumentException.ThrowIfNullOrEmpty(texture);
        return new TextureScope(texture);
    }

    /// <summary>Gets the value this scope sets.</summary>
    /// <param name="setting">The setting.</param>
    /// <param name="settings">Model import settings, a JSON object.</param>
    /// <returns>The value, or <see langword="null"/> when the scope does not set it.</returns>
    public abstract object? Read(ModelImportSetting setting, JsonElement settings);

    /// <summary>Gets the value that applies where this scope sets none.</summary>
    /// <param name="setting">The setting.</param>
    /// <param name="settings">Model import settings, a JSON object.</param>
    /// <returns>The inherited value, or <see langword="null"/> when the importer decides from the file.</returns>
    public abstract object? Inherited(ModelImportSetting setting, JsonElement settings);

    /// <summary>Gets the settings with the value set in this scope, or removed for <see langword="null"/>.</summary>
    /// <param name="setting">The setting.</param>
    /// <param name="settings">Model import settings, a JSON object.</param>
    /// <param name="value">The value, or <see langword="null"/> to remove it.</param>
    /// <returns>The changed settings.</returns>
    public abstract JsonElement Write(ModelImportSetting setting, JsonElement settings, object? value);

    private sealed record ModelScope : ModelImportSettingScope
    {
        public override object? Read(ModelImportSetting setting, JsonElement settings)
        {
            ArgumentNullException.ThrowIfNull(setting);
            return setting.ReadFrom(settings);
        }

        public override object? Inherited(ModelImportSetting setting, JsonElement settings)
        {
            ArgumentNullException.ThrowIfNull(setting);
            return setting.Default;
        }

        public override JsonElement Write(ModelImportSetting setting, JsonElement settings, object? value)
        {
            ArgumentNullException.ThrowIfNull(setting);
            return setting.WriteTo(settings, value);
        }
    }

    private sealed record TextureScope(string Name) : ModelImportSettingScope
    {
        public override object? Read(ModelImportSetting setting, JsonElement settings)
        {
            ArgumentNullException.ThrowIfNull(setting);
            return this.OverridesOf(settings) is { } overrides ? setting.ReadFrom(overrides) : null;
        }

        public override object? Inherited(ModelImportSetting setting, JsonElement settings)
        {
            ArgumentNullException.ThrowIfNull(setting);
            return setting.ReadFrom(settings) ?? setting.Default;
        }

        // An override left empty is removed, and then an empty map, so the settings hold only what the user set.
        public override JsonElement Write(ModelImportSetting setting, JsonElement settings, object? value)
        {
            ArgumentNullException.ThrowIfNull(setting);
            var overrides = setting.WriteTo(this.OverridesOf(settings) ?? JsonSerializer.SerializeToElement(new JsonObject()), value);
            var copy = settings.ValueKind == JsonValueKind.Object ? JsonNode.Parse(settings.GetRawText())!.AsObject() : [];
            var textures = copy[ModelImportSettingCatalog.TextureOverridesKey]?.DeepClone() as JsonObject ?? [];
            if (overrides.EnumerateObject().Any())
            {
                textures[this.Name] = JsonNode.Parse(overrides.GetRawText());
            }
            else
            {
                _ = textures.Remove(this.Name);
            }

            if (textures.Count == 0)
            {
                _ = copy.Remove(ModelImportSettingCatalog.TextureOverridesKey);
            }
            else
            {
                copy[ModelImportSettingCatalog.TextureOverridesKey] = textures;
            }

            return JsonSerializer.SerializeToElement(copy);
        }

        private JsonElement? OverridesOf(JsonElement settings)
            => settings.ValueKind == JsonValueKind.Object
                && settings.TryGetProperty(ModelImportSettingCatalog.TextureOverridesKey, out var textures)
                && textures.ValueKind == JsonValueKind.Object
                && textures.TryGetProperty(this.Name, out var overrides)
                && overrides.ValueKind == JsonValueKind.Object
                    ? overrides
                    : null;
    }
}
