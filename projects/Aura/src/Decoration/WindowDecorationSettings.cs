// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System;
using System.Collections.Generic;

namespace DroidNet.Aura.Decoration;

/// <summary>
/// Persistent window decoration configuration including category defaults and per-type overrides.
/// </summary>
/// <remarks>
/// <para>
/// <see cref="WindowDecorationSettings"/> stores presets for window categories and optional overrides for
/// specific window types. The settings are serialized directly using <see cref="System.Text.Json"/>.
/// </para>
/// <para>
/// Dictionaries use case-insensitive keys for categories and case-sensitive keys for fully qualified window
/// type names. Stored <see cref="WindowDecorationOptions"/> instances are immutable records and should be
/// validated by callers before persisting.
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
    /// Gets the default decoration options indexed by semantic window category (e.g., "Primary").
    /// </summary>
    /// <value>
    /// A dictionary keyed by category name using <see cref="StringComparer.OrdinalIgnoreCase"/>.
    /// </value>
    public IDictionary<string, WindowDecorationOptions> DefaultsByCategory { get; }
        = new Dictionary<string, WindowDecorationOptions>(StringComparer.OrdinalIgnoreCase);

    /// <summary>
    /// Gets explicit decoration overrides indexed by window type name.
    /// </summary>
    /// <value>
    /// A dictionary keyed by fully qualified window type name using <see cref="StringComparer.Ordinal"/>.
    /// </value>
    public IDictionary<string, WindowDecorationOptions> OverridesByType { get; }
        = new Dictionary<string, WindowDecorationOptions>(StringComparer.Ordinal);
}
