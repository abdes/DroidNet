// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Controls.OutputLog.Rendering;

internal static class CasingTransform
{
    /// <summary>
    /// Apply upper or lower casing to <paramref name="value" /> when <paramref name="format" /> is provided.
    /// Returns <paramref name="value" /> when no or invalid format provided.
    /// </summary>
    /// <param name="value">Provided string for formatting.</param>
    /// <param name="format">Format string.</param>
    /// <returns>The provided <paramref name="value" /> with formatting applied.</returns>
    public static string Apply(string value, string? format = default)
        => format switch
        {
            "u" => value.ToUpperInvariant(),
            "w" => value.ToLowerInvariant(),
            _ => value,
        };
}
