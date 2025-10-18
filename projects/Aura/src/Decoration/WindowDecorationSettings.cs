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
/// The dictionary is keyed by WindowCategory constants. Stored <see cref="WindowDecorationOptions"/>
/// instances are immutable records and should be validated before persisting.
/// </para>
/// <para>
/// Code-defined defaults for standard categories (Main, Secondary, Tool, Document, Unknown) are maintained
/// separately in <see cref="WindowDecorationSettingsService"/> and are not persisted.
/// </para>
/// </remarks>
public sealed class WindowDecorationSettings : IWindowDecorationSettings
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
    /// A dictionary keyed by category containing user/application-specific
    /// customizations that override the built-in defaults.
    /// </value>
    public IDictionary<WindowCategory, WindowDecorationOptions> CategoryOverrides { get; init; }
        = new Dictionary<WindowCategory, WindowDecorationOptions>();

    /// <inheritdoc/>
    IReadOnlyDictionary<WindowCategory, WindowDecorationOptions> IWindowDecorationSettings.CategoryOverrides
        => (IReadOnlyDictionary<WindowCategory, WindowDecorationOptions>)this.CategoryOverrides;

    /// <inheritdoc/>
    public WindowDecorationOptions GetEffectiveDecoration(WindowCategory category)
        => throw new InvalidOperationException("Use the service `Settings` property to call methods.");

    /// <inheritdoc/>
    public void SetCategoryOverride(WindowCategory category, WindowDecorationOptions options)
        => throw new InvalidOperationException("Use the service `Settings` property to call methods.");

    /// <inheritdoc/>
    public bool RemoveCategoryOverride(WindowCategory category)
        => throw new InvalidOperationException("Use the service `Settings` property to call methods.");
}
