// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json.Serialization;
using Microsoft.UI.Xaml;

namespace DroidNet.Aura.Decoration.Serialization;

/// <summary>
/// JSON source generator context for window decoration types.
/// </summary>
/// <remarks>
/// <para>
/// This context provides source-generated JSON serialization for all window decoration
/// types, enabling AOT compilation and improved performance. It configures the serializer
/// to use camel-case property names, write indented JSON, and omit null properties.
/// </para>
/// <para>
/// Enums are serialized as strings for readability, and the custom
/// <see cref="MenuOptionsJsonConverter"/> is used for <see cref="MenuOptions"/> to ensure
/// only the provider ID is persisted (not the menu source).
/// </para>
/// </remarks>
/// <example>
/// <code>
/// var options = new JsonSerializerOptions
/// {
///     TypeInfoResolver = WindowDecorationJsonContext.Default
/// };
///
/// var json = JsonSerializer.Serialize(decorationOptions, options);
/// var deserialized = JsonSerializer.Deserialize&lt;WindowDecorationOptions&gt;(json, options);
/// </code>
/// </example>
[JsonSourceGenerationOptions(
    WriteIndented = true,
    DefaultIgnoreCondition = JsonIgnoreCondition.WhenWritingNull,
    Converters = [
        typeof(WindowCategoryJsonConverter),
        typeof(JsonStringEnumConverter<BackdropKind>),
        typeof(JsonStringEnumConverter<DragRegionBehavior>),
        typeof(JsonStringEnumConverter<ButtonPlacement>)])]
[JsonSerializable(typeof(WindowDecorationSettings))]
[JsonSerializable(typeof(WindowDecorationOptions))]
[JsonSerializable(typeof(TitleBarOptions))]
[JsonSerializable(typeof(WindowButtonsOptions))]
[JsonSerializable(typeof(MenuOptions))]
[JsonSerializable(typeof(BackdropKind))]
[JsonSerializable(typeof(DragRegionBehavior))]
[JsonSerializable(typeof(ButtonPlacement))]
[JsonSerializable(typeof(Dictionary<WindowCategory, WindowDecorationOptions>))]
public partial class WindowDecorationJsonContext : JsonSerializerContext
{
}
