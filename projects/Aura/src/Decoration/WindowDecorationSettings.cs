// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Aura.Decoration;

/// <summary>
/// Persistent window decoration configuration containing category-based overrides.
/// </summary>
/// <remarks>
/// <para>
/// <see cref="WindowDecorationSettings"/> stores user/application-specific customizations that override
/// the code-defined defaults for window categories. The settings are serialized using <see cref="System.Text.Json"/>.
/// </para>
/// <para>
/// The dictionary uses case-insensitive keys for category names to match the behavior of
/// <see cref="WindowManagement.WindowCategory"/> constants. Stored <see cref="WindowDecorationOptions"/>
/// instances are immutable records and should be validated before persisting.
/// </para>
/// <para>
/// Code-defined defaults for standard categories (Main, Secondary, Tool, Document, Unknown) are maintained
/// separately in <see cref="WindowDecorationSettingsService"/> and are not persisted.
/// </para>
/// </remarks>
public sealed class WindowDecorationSettings
{
    /// <summary>
    /// The name of the configuration file where the decoration settings are stored.
    /// </summary>
    public const string ConfigFileName = "Aura.json";

    /// <summary>
    /// The configuration section name used when persisting decoration settings.
    /// </summary>
    public const string ConfigSectionName = nameof(WindowDecorationSettings);

    /// <summary>
    /// Gets the category-based decoration overrides that supersede code-defined defaults.
    /// </summary>
    /// <value>
    /// A dictionary keyed by category name (case-insensitive) containing user/application-specific
    /// customizations that override the built-in defaults.
    /// </value>
    public IDictionary<WindowCategory, WindowDecorationOptions> CategoryOverrides { get; }
        = new Dictionary<WindowCategory, WindowDecorationOptions>();
}
