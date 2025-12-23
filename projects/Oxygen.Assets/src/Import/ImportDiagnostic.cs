// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Assets.Import;

/// <summary>
/// Represents a structured diagnostic produced by import/build.
/// </summary>
/// <param name="Severity">Diagnostic severity.</param>
/// <param name="Code">A stable diagnostic code.</param>
/// <param name="Message">Human-readable diagnostic message.</param>
/// <param name="SourcePath">Optional project-relative source path.</param>
/// <param name="VirtualPath">Optional virtual path associated with the diagnostic.</param>
public sealed record ImportDiagnostic(
    ImportDiagnosticSeverity Severity,
    string Code,
    string Message,
    string? SourcePath = null,
    string? VirtualPath = null);
