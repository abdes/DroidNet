// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Controls;

/// <summary>How serious a <see cref="DiagnosticCard"/>'s finding is, which its background shows.</summary>
public enum DiagnosticCardSeverity
{
    /// <summary>Something worth knowing; the card has the plain card background.</summary>
    Informational,

    /// <summary>Something that will not be as authored; the card has the caution background.</summary>
    Warning,

    /// <summary>Something that fails; the card has the critical background.</summary>
    Error,
}
