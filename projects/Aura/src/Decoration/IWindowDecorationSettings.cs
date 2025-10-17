// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Aura.Decoration;

/// <summary>
/// Represents the window decoration settings data.
/// </summary>
/// <remarks>
/// <para>
/// This interface provides access to persisted category-based decoration overrides.
/// It does not include code-defined defaults, which are managed separately by the settings service.
/// </para>
/// </remarks>
public interface IWindowDecorationSettings
{
    /// <summary>
    /// Gets the category-based decoration overrides that supersede code-defined defaults.
    /// </summary>
    /// <value>
    /// A dictionary keyed by category name (case-insensitive) containing user/application-specific
    /// customizations that override the built-in defaults.
    /// </value>
    public IReadOnlyDictionary<string, WindowDecorationOptions> CategoryOverrides { get; }
}
