// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Core.Diagnostics;

/// <summary>
/// Severity of a diagnostic record.
/// </summary>
public enum DiagnosticSeverity
{
    /// <summary>
    /// Informational diagnostic.
    /// </summary>
    Info,

    /// <summary>
    /// Warning diagnostic.
    /// </summary>
    Warning,

    /// <summary>
    /// Error diagnostic.
    /// </summary>
    Error,

    /// <summary>
    /// Fatal diagnostic.
    /// </summary>
    Fatal,
}
