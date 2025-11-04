// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json;
using System.Text.Json.Serialization;

namespace DroidNet.Aura;

/// <summary>
///     Defines standard window category constants used for classifying windows.
/// </summary>
/// <remarks>
///     Window categories are semantic identifiers that describe the role or purpose of a window within the application.
///     These constants provide a standardized vocabulary for window classification, enabling type-safe window
///     management operations and queries.
/// </remarks>
[JsonConverter(typeof(WindowCategoryJsonConverter))]
public readonly record struct WindowCategory
{
    /// <summary>
    ///     Represents the main application window.
    /// </summary>
    /// <remarks>
    ///     The main window typically hosts the primary user interface and serves as the central hub for the
    ///     application. Most applications have exactly one main window.
    /// </remarks>
    public static readonly WindowCategory Main = new("main");

    /// <summary>
    ///     Represents a secondary window.
    /// </summary>
    /// <remarks>
    ///     Secondary windows are additional windows that complement the main window, often used for auxiliary views or content
    ///     that benefits from being displayed in a separate window. These are typically created through routing or navigation.
    /// </remarks>
    public static readonly WindowCategory Secondary = new("secondary");

    /// <summary>
    /// Represents a document window.
    /// </summary>
    /// <remarks>
    ///     Document windows host editable content such as text files, images, or other user-created artifacts. Applications often
    ///     support multiple document windows simultaneously in a multi-document interface (MDI) pattern.
    /// </remarks>
    public static readonly WindowCategory Document = new("document");

    /// <summary>
    ///     Represents a tool window (palette, inspector, etc.).
    /// </summary>
    /// <remarks>
    ///     Tool windows are auxiliary windows that provide specialized functionality or information, such as property
    ///     inspectors, toolboxes, or debug consoles. They typically remain visible alongside the main content area.
    /// </remarks>
    public static readonly WindowCategory Tool = new("tool");

    /// <summary>
    ///     Represents a transient window (e.g., floating inspector, temporary overlay).
    /// </summary>
    /// <remarks>
    ///     Transient windows are short-lived, often layered above other content, and typically used for quick interactions such
    ///     as popups, floating tool panels, or inspectors. They may have custom appearance and are not intended to persist.
    /// </remarks>
    public static readonly WindowCategory Transient = new("transient");

    /// <summary>
    ///     Represents a modal window (e.g., dialog, blocking prompt).
    /// </summary>
    /// <remarks>
    ///     Modal windows require user interaction before returning to the underlying content. They are used for confirmations,
    ///     alerts, or other blocking UI flows, and typically prevent interaction with other windows until dismissed.
    /// </remarks>
    public static readonly WindowCategory Modal = new("modal");

    /// <summary>
    ///     Represents a window with no border and no title bar.
    /// </summary>
    /// <remarks>
    ///     Frameless windows are typically used for immersive applications or when a minimalistic
    ///     design is desired. They are still fully fledged windows, that can have XAML content.
    /// </remarks>
    public static readonly WindowCategory Frameless = new("frameless");

    /// <summary>
    ///     Represents the category of windows, that are not decorated by Aura.
    /// </summary>
    /// <remarks>
    ///     This category serves as a fallback for windows that don't fit into the predefined categories or when the
    ///     window type is not explicitly specified.
    /// </remarks>
    public static readonly WindowCategory System = new("system");

    /// <summary>
    ///     Initializes a new instance of the <see cref="WindowCategory"/> struct with the specified value.
    /// </summary>
    /// <param name="value">
    ///     The category string. Whitespace is trimmed; a null value will result in the <see cref="System"/> category being used.
    /// </param>
    /// <exception cref="ValidationException">Thrown when <paramref name="value"/> is empty or whitespace.</exception>
    public WindowCategory(string? value)
    {
        value = value?.Trim();

        // Validate Category
        if (value?.Length == 0)
        {
            throw new ValidationException("Window category cannot be empty or whitespace.");
        }

        this.Value = value ?? System.Value;
    }

    /// <summary>
    ///     Gets the canonical string value of the window category.
    /// </summary>
    /// <remarks>
    ///     The value is case-insensitive and represents the serialized form used for JSON and dictionary keys. It will never be
    ///     null; when a null input is supplied to the constructor the <see cref="System"/> category value is used.
    /// </remarks>
    public string Value { get; }

    /// <summary>
    ///     Returns the string representation of the window category.
    /// </summary>
    /// <returns>The canonical category string.</returns>
    public override string ToString() => this.Value;

    /// <summary>
    ///     Returns a case-insensitive hash code for the category value.
    /// </summary>
    /// <returns>An integer hash code.</returns>
    public override int GetHashCode() => this.Value.GetHashCode(StringComparison.OrdinalIgnoreCase);

    /// <summary>
    ///     Determines whether the specified <see cref="WindowCategory"/> is equal to the current
    ///     instance using a case-insensitive comparison of the underlying values.
    /// </summary>
    /// <param name="other">The other <see cref="WindowCategory"/> to compare.</param>
    /// <returns><see langword="true"/> if the two categories are equal; otherwise, <see langword="false"/>.</returns>
    public bool Equals(WindowCategory other) => string.Equals(this.Value, other.Value, StringComparison.OrdinalIgnoreCase);
}

/// <summary>
///     JSON converter for <see cref="WindowCategory"/> that handles string-based
///     serialization and deserialization as well as dictionary property-name support.
/// </summary>
/// <remarks>
///     This converter writes and reads the canonical <see cref="WindowCategory.Value"/> as a JSON string. It also
///     implements <c>ReadAsPropertyName</c> and <c>WriteAsPropertyName</c> so <see cref="WindowCategory"/> can be used
///     as a dictionary key when using System.Text.Json.
/// </remarks>
public class WindowCategoryJsonConverter : JsonConverter<WindowCategory>
{
    /// <summary>
    ///     Reads a <see cref="WindowCategory"/> from a JSON string value.
    /// </summary>
    /// <param name="reader">The JSON reader positioned on the string token.</param>
    /// <param name="typeToConvert">The target type to convert (ignored).</param>
    /// <param name="options">Serializer options (ignored).</param>
    /// <returns>A new <see cref="WindowCategory"/> instance with the parsed value.</returns>
    public override WindowCategory Read(ref Utf8JsonReader reader, Type typeToConvert, JsonSerializerOptions options)
        => new(reader.GetString());

    /// <summary>
    ///     Writes the <see cref="WindowCategory"/> as a JSON string value.
    /// </summary>
    /// <param name="writer">The JSON writer to write into.</param>
    /// <param name="value">The category value to serialize.</param>
    /// <param name="options">Serializer options (ignored).</param>
    public override void Write(Utf8JsonWriter writer, WindowCategory value, JsonSerializerOptions options)
        => writer.WriteStringValue(value.Value);

    /// <summary>
    ///     Deserializes a property name into a <see cref="WindowCategory"/>.
    /// </summary>
    /// <param name="reader">The JSON reader positioned on a property name token.</param>
    /// <param name="typeToConvert">The target type to convert (ignored).</param>
    /// <param name="options">Serializer options (ignored).</param>
    /// <returns>A <see cref="WindowCategory"/> created from the property name.</returns>
    /// <remarks>
    ///     `System.Text.Json will` call these when serializing/deserializing dictionary keys so WindowCategory can be
    ///     used as a dictionary key type.
    /// </remarks>
    public override WindowCategory ReadAsPropertyName(ref Utf8JsonReader reader, Type typeToConvert, JsonSerializerOptions options)
        => new(reader.GetString());

    /// <summary>
    ///     Writes a <see cref="WindowCategory"/> as a raw JSON property name.
    /// </summary>
    /// <param name="writer">The JSON writer to write into.</param>
    /// <param name="value">The category to write as the property name.</param>
    /// <param name="options">Serializer options (ignored).</param>
    public override void WriteAsPropertyName(Utf8JsonWriter writer, WindowCategory value, JsonSerializerOptions options)
        => writer.WritePropertyName(value.Value);
}
