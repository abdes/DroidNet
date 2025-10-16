// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Generic;

namespace DroidNet.Aura.Decoration;

/// <summary>
/// Provides domain-specific accessors for window decoration defaults and overrides.
/// </summary>
public interface IWindowDecorationSettings
{
    /// <summary>
    /// Gets the configured defaults indexed by decoration category.
    /// </summary>
    public IReadOnlyDictionary<string, WindowDecorationOptions> DefaultsByCategory { get; }

    /// <summary>
    /// Gets the configured per-window overrides indexed by window type.
    /// </summary>
    public IReadOnlyDictionary<string, WindowDecorationOptions> OverridesByType { get; }

    /// <summary>
    /// Retrieves the default decoration options for the specified category if present.
    /// </summary>
    /// <param name="category">The semantic category name.</param>
    /// <returns>The configured decoration options or <see langword="null"/> when not found.</returns>
    public WindowDecorationOptions? GetDefaultForCategory(string category);

    /// <summary>
    /// Retrieves the per-window override for the specified type if present.
    /// </summary>
    /// <param name="windowType">The fully qualified window type name.</param>
    /// <returns>The configured decoration options or <see langword="null"/> when not found.</returns>
    public WindowDecorationOptions? GetOverrideForType(string windowType);

    /// <summary>
    /// Stores a default decoration for the specified category, replacing any existing value.
    /// </summary>
    /// <param name="category">The semantic category name.</param>
    /// <param name="options">The decoration options to persist.</param>
    public void SetDefaultForCategory(string category, WindowDecorationOptions options);

    /// <summary>
    /// Removes the configured default decoration for the specified category when present.
    /// </summary>
    /// <param name="category">The semantic category name.</param>
    /// <returns><see langword="true"/> when an entry was removed; otherwise <see langword="false"/>.</returns>
    public bool RemoveDefaultForCategory(string category);

    /// <summary>
    /// Stores a per-window override for the specified type, replacing any existing value.
    /// </summary>
    /// <param name="windowType">The fully qualified window type name.</param>
    /// <param name="options">The decoration options to persist.</param>
    public void SetOverrideForType(string windowType, WindowDecorationOptions options);

    /// <summary>
    /// Removes the configured per-window override for the specified type when present.
    /// </summary>
    /// <param name="windowType">The fully qualified window type name.</param>
    /// <returns><see langword="true"/> when an entry was removed; otherwise <see langword="false"/>.</returns>
    public bool RemoveOverrideForType(string windowType);
}
