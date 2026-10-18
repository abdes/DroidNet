// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using System.Globalization;
using System.Text;
using Microsoft.CodeAnalysis;
using Microsoft.CodeAnalysis.Text;

namespace Oxygen.Managed.Core.Generators;

/// <summary>
/// Generates <c>UnicodeTables</c>, the Unicode data of the virtual path rules, from the pinned Unicode Character
/// Database given as additional files: the assigned code points (<c>DerivedAge.txt</c>) and the simple case folding
/// (<c>CaseFolding.txt</c>, status C and S). The engine keeps the database in its source tree and generates its own
/// tables from the same files, so both implementations of the rules use one data set. The compiler reruns the
/// generator only when one of the files changes.
/// </summary>
[SuppressMessage("RoslynDiagnostics", "RS2008", Justification = "A build-time generator of this repository, not a shipped analyzer package.")]
[Generator]
public sealed class UnicodeTablesGenerator : IIncrementalGenerator
{
    private const string AgeFile = "DerivedAge.txt";
    private const string FoldingFile = "CaseFolding.txt";
    private const int ValuesPerLine = 8;

    private static readonly string[] RangeSeparator = [".."];

    private static readonly DiagnosticDescriptor MissingDatabase = new(
        id: "OXGNUT001",
        title: "Unicode database missing",
        messageFormat: "The Unicode tables need the additional files DerivedAge.txt and CaseFolding.txt",
        category: "Oxygen.Managed.Core.Generator",
        DiagnosticSeverity.Error,
        isEnabledByDefault: true);

    private static readonly DiagnosticDescriptor InvalidDatabase = new(
        id: "OXGNUT002",
        title: "Unicode database invalid",
        messageFormat: "The Unicode database cannot be read: {0}",
        category: "Oxygen.Managed.Core.Generator",
        DiagnosticSeverity.Error,
        isEnabledByDefault: true);

    /// <inheritdoc/>
    public void Initialize(IncrementalGeneratorInitializationContext context)
    {
        var files = context.AdditionalTextsProvider
            .Where(static file => IsDatabaseFile(file.Path))
            .Select(static (file, cancellation) => (Name: Path.GetFileName(file.Path), Text: file.GetText(cancellation)?.ToString() ?? string.Empty))
            .Collect();

        context.RegisterSourceOutput(files, static (output, database) =>
        {
            var age = database.FirstOrDefault(static file => string.Equals(file.Name, AgeFile, StringComparison.Ordinal)).Text;
            var folding = database.FirstOrDefault(static file => string.Equals(file.Name, FoldingFile, StringComparison.Ordinal)).Text;
            if (age is null || folding is null)
            {
                output.ReportDiagnostic(Diagnostic.Create(MissingDatabase, Location.None));
                return;
            }

            try
            {
                var version = VersionOf(age, AgeFile);
                if (!string.Equals(version, VersionOf(folding, FoldingFile), StringComparison.Ordinal))
                {
                    throw new FormatException("DerivedAge.txt and CaseFolding.txt are of different Unicode versions");
                }

                output.AddSource("UnicodeTables.g.cs", SourceText.From(Render(version, ParseAssigned(age), ParseSimpleCaseFolding(folding)), Encoding.UTF8));
            }
            catch (FormatException ex)
            {
                output.ReportDiagnostic(Diagnostic.Create(InvalidDatabase, Location.None, ex.Message));
            }
        });
    }

    private static bool IsDatabaseFile(string path)
    {
        var name = Path.GetFileName(path);
        return string.Equals(name, AgeFile, StringComparison.Ordinal) || string.Equals(name, FoldingFile, StringComparison.Ordinal);
    }

    private static string VersionOf(string text, string name)
    {
        // The first line names the file and its version, such as "# DerivedAge-15.1.0.txt".
        var first = text.Split('\n')[0].Trim();
        var prefix = "# " + Path.GetFileNameWithoutExtension(name) + "-";
        return first.StartsWith(prefix, StringComparison.Ordinal) && first.EndsWith(".txt", StringComparison.Ordinal)
            ? first.Substring(prefix.Length, first.Length - prefix.Length - ".txt".Length)
            : throw new FormatException(name + " does not start with its version line");
    }

    private static IEnumerable<string[]> DataLines(string text)
    {
        foreach (var line in text.Split('\n'))
        {
            var hash = line.IndexOf('#');
            var content = (hash < 0 ? line : line.Substring(0, hash)).Trim();
            if (content.Length != 0)
            {
                yield return content.Split(';').Select(static field => field.Trim()).ToArray();
            }
        }
    }

    private static int ParseCode(string text)
        => int.TryParse(text, NumberStyles.HexNumber, CultureInfo.InvariantCulture, out var code)
            ? code
            : throw new FormatException("'" + text + "' is not a code point");

    private static List<(int First, int Last)> ParseAssigned(string text)
    {
        var ranges = new List<(int First, int Last)>();
        foreach (var fields in DataLines(text))
        {
            var bounds = fields[0].Split(RangeSeparator, StringSplitOptions.None);
            ranges.Add((ParseCode(bounds[0]), ParseCode(bounds[bounds.Length - 1])));
        }

        ranges.Sort();
        var merged = new List<(int First, int Last)>();
        foreach (var (first, last) in ranges)
        {
            if (merged.Count != 0 && first <= merged[merged.Count - 1].Last + 1)
            {
                merged[merged.Count - 1] = (merged[merged.Count - 1].First, Math.Max(merged[merged.Count - 1].Last, last));
            }
            else
            {
                merged.Add((first, last));
            }
        }

        return merged;
    }

    private static List<(int Code, int Folded)> ParseSimpleCaseFolding(string text)
    {
        var pairs = new SortedDictionary<int, int>();
        foreach (var fields in DataLines(text))
        {
            if (fields.Length >= 3 && fields[1] is "C" or "S")
            {
                var code = ParseCode(fields[0]);
                if (pairs.ContainsKey(code))
                {
                    throw new FormatException("CaseFolding.txt maps U+" + fields[0] + " twice");
                }

                pairs.Add(code, ParseCode(fields[2]));
            }
        }

        return [.. pairs.Select(static pair => (pair.Key, pair.Value))];
    }

    private static string Render(string version, List<(int First, int Last)> assigned, List<(int Code, int Folded)> folding)
    {
        var source = new StringBuilder()
            .AppendLine("// <auto-generated>")
            .Append("// Generated by UnicodeTablesGenerator from Unicode ").Append(version).AppendLine(" data.")
            .AppendLine("// </auto-generated>")
            .AppendLine()
            .AppendLine("namespace Oxygen.Managed.Core;")
            .AppendLine()
            .AppendLine("/// <summary>Unicode tables of the virtual path rules.</summary>")
            .AppendLine("internal static class UnicodeTables")
            .AppendLine("{")
            .AppendLine("    /// <summary>The Unicode version of the tables.</summary>")
            .Append("    public const string Version = \"").Append(version).AppendLine("\";");
        AppendArray(source, "AssignedFirst", "First code point of each assigned range, in order.", assigned.Select(static range => range.First));
        AppendArray(source, "AssignedLast", "Last code point of each assigned range, in order.", assigned.Select(static range => range.Last));
        AppendArray(source, "FoldingCodes", "Code points with a simple case folding, in order.", folding.Select(static pair => pair.Code));
        AppendArray(source, "FoldingTargets", "The simple case folding of each of FoldingCodes.", folding.Select(static pair => pair.Folded));
        return source.AppendLine("}").ToString();
    }

    private static void AppendArray(StringBuilder source, string name, string summary, IEnumerable<int> values)
    {
        _ = source.AppendLine()
            .Append("    /// <summary>").Append(summary).AppendLine("</summary>")
            .Append("    public static global::System.ReadOnlySpan<int> ").Append(name).AppendLine(" =>")
            .AppendLine("    [");
        List<string> items = [.. values.Select(static value => "0x" + value.ToString("X6", CultureInfo.InvariantCulture) + ",")];
        for (var index = 0; index < items.Count; index += ValuesPerLine)
        {
            _ = source.Append("        ").AppendLine(string.Join(" ", items.Skip(index).Take(ValuesPerLine)));
        }

        _ = source.AppendLine("    ];");
    }
}
