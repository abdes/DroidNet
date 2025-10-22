// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Aura.Decoration;

namespace DroidNet.Aura.Settings;

/// <summary>
/// Represents the window decoration settings data interface with all domain-specific operations.
/// </summary>
/// <remarks>
/// <para>
/// This interface provides access to window decoration settings data AND all methods needed
/// to manage them. Access via ISettingsService&lt;IWindowDecorationSettings&gt;.Settings.
/// </para>
/// </remarks>
public interface IWindowDecorationSettings
{
    /// <summary>
    /// Gets the category-based decoration overrides that supersede code-defined defaults.
    /// </summary>
    /// <value>
    /// A read-only dictionary keyed by category containing user/application-specific
    /// customizations that override the built-in defaults.
    /// </value>
    public IReadOnlyDictionary<WindowCategory, WindowDecorationOptions> CategoryOverrides { get; }

    /// <summary>
    /// Resolves the effective decoration options for a given window category.
    /// </summary>
    /// <param name="category">The window category.</param>
    /// <returns>
    /// The effective decoration options combining code-defined defaults with any persisted overrides.
    /// </returns>
    public WindowDecorationOptions GetEffectiveDecoration(WindowCategory category);

    /// <summary>
    /// Sets a persisted override for a window category, replacing any existing override.
    /// </summary>
    /// <param name="category">The window category to override.</param>
    /// <param name="options">The decoration options to persist as an override.</param>
    public void SetCategoryOverride(WindowCategory category, WindowDecorationOptions options);

    /// <summary>
    /// Removes a persisted category override, reverting to the code-defined default.
    /// </summary>
    /// <param name="category">The window category to revert to default.</param>
    /// <returns>
    /// <see langword="true"/> if an override was removed; <see langword="false"/> otherwise.
    /// </returns>
    public bool RemoveCategoryOverride(WindowCategory category);
}
