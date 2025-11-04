// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json.Serialization;
using DroidNet.Aura.Settings;

namespace DroidNet.Aura.Decoration.Serialization;

/// <summary>
///     JSON source generator context for window decoration types.
/// </summary>
/// <remarks>
///     This context provides source-generated JSON serialization for all window decoration types,
///     enabling AOT compilation and improved performance. It configures the serializer to use
///     camel-case property names, write indented JSON, and omit null properties.
///     <para>
///     Enums are serialized as strings for readability, and the custom <see
///     cref="MenuOptionsJsonConverter"/> is used for <see cref="MenuOptions"/> to ensure only the
///     provider ID is persisted (not the menu source).</para>
/// </remarks>
/// <example>
///     <code><![CDATA[
///     var options = new JsonSerializerOptions
///     {
///         TypeInfoResolver = WindowDecorationJsonContext.Default
///     };
///
///     var json = JsonSerializer.Serialize(decorationOptions, options);
///     var deserialized = JsonSerializer.Deserialize&lt;WindowDecorationOptions&gt;(json, options);
///     ]]></code>
/// </example>
[JsonSourceGenerationOptions(
    WriteIndented = true,
    PropertyNamingPolicy = JsonKnownNamingPolicy.CamelCase,
    DefaultIgnoreCondition = JsonIgnoreCondition.WhenWritingNull,
    Converters =
    [
        typeof(WindowCategoryJsonConverter),
        typeof(JsonStringEnumConverter<BackdropKind>),
        typeof(JsonStringEnumConverter<DragRegionBehavior>),
    ])]
[JsonSerializable(typeof(WindowDecorationSettings))]
[JsonSerializable(typeof(WindowDecorationOptions))]
[JsonSerializable(typeof(TitleBarOptions))]
[JsonSerializable(typeof(WindowButtonsOptions))]
[JsonSerializable(typeof(MenuOptions))]
[JsonSerializable(typeof(BackdropKind))]
[JsonSerializable(typeof(DragRegionBehavior))]
[JsonSerializable(typeof(Dictionary<WindowCategory, WindowDecorationOptions>))]
[System.Diagnostics.CodeAnalysis.SuppressMessage("StyleCop.CSharp.ReadabilityRules", "SA1118:Parameter should not span multiple lines", Justification = "more readable like this")]
public partial class WindowDecorationJsonContext : JsonSerializerContext;
