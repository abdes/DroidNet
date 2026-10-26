// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using System.Globalization;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.ModelEditor.Importing;

/// <summary>One finding of a file's analysis: a diagnostic the import will report, once, with every object of the file
/// it is about.</summary>
/// <remarks>
/// The engine reports a finding for each object it is about, such as each material whose texture sampling is left out;
/// Before import shows it once and names the objects, so thirty materials make one item, not thirty.
/// </remarks>
/// <param name="Severity">The finding's severity.</param>
/// <param name="Code">The diagnostic code.</param>
/// <param name="Message">What is left out and what the content is instead.</param>
/// <param name="Objects">The names of the objects it is about, in the file's order; empty when it is about the whole file.</param>
public sealed record BeforeImportItem(DiagnosticSeverity Severity, string Code, string Message, ImmutableArray<string> Objects)
{
    /// <summary>Groups a file's diagnostics into findings: one per code, severity and message, in the order they first
    /// occur, each naming the objects its diagnostics are about.</summary>
    /// <param name="diagnostics">The file's diagnostics.</param>
    /// <returns>The findings.</returns>
    public static ImmutableArray<BeforeImportItem> From(IEnumerable<DiagnosticRecord> diagnostics)
    {
        ArgumentNullException.ThrowIfNull(diagnostics);
        return
        [
            .. diagnostics
                .GroupBy(static issue => (issue.Severity, issue.Code, issue.Message))
                .Select(static finding => new BeforeImportItem(
                    finding.Key.Severity,
                    finding.Key.Code,
                    finding.Key.Message,
                    [.. finding.Select(ObjectOf).OfType<string>().Distinct(StringComparer.Ordinal)])),
        ];
    }

    // The name the file gives the object, or, for an unnamed one, its kind and index in the file ("material #3").
    private static string? ObjectOf(DiagnosticRecord issue)
    {
        if (issue.AffectedEntity?.SourceObjectName is { Length: > 0 } name)
        {
            return name;
        }

        var segments = issue.AffectedEntity?.SourceObjectPath?.Split('/', StringSplitOptions.RemoveEmptyEntries) ?? [];
        if (segments.Length < 2 || !int.TryParse(segments[1], NumberStyles.None, CultureInfo.InvariantCulture, out var index))
        {
            return null;
        }

        var kind = segments[0].ToUpperInvariant() switch
        {
            "MATERIALS" => "material",
            "MESHES" => "mesh",
            "NODES" => "node",
            _ => null,
        };
        return kind is null ? null : string.Create(CultureInfo.CurrentCulture, $"{kind} #{index}");
    }
}
