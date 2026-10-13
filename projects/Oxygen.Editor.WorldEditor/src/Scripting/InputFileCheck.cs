// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json;
using System.Text.RegularExpressions;
using Json.Schema;
using Oxygen.Editor.ContentBrowser.Scripts;
using Oxygen.Editor.ContentPipeline;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.World.Scripting;

/// <summary>
/// Checks a hand-written input file the way its cook will: valid JSON, valid against the engine's input schema, and
/// self-contained.
/// </summary>
public static partial class InputFileCheck
{
    private const int MaxSchemaProblems = 10;

    private static readonly EvaluationOptions Evaluation = new() { OutputFormat = OutputFormat.List };

    /// <summary>Checks an input file.</summary>
    /// <param name="path">The input file.</param>
    /// <param name="text">The file's text.</param>
    /// <param name="schema">The engine schema for the file's kind, or <see langword="null"/> when the SDK has none.</param>
    /// <returns>The problems, empty when the file cooks.</returns>
    public static IReadOnlyList<ScriptProblem> Check(string path, string text, JsonSchema? schema)
    {
        ArgumentNullException.ThrowIfNull(text);
        JsonDocument document;
        try
        {
            document = JsonDocument.Parse(text);
        }
        catch (JsonException exception)
        {
            var line = (int)(exception.LineNumber ?? 0) + 1;
            var column = (int)(exception.BytePositionInLine ?? 0) + 1;
            return [Problem(path, ScriptDiagnosticCodes.InputInvalid, "the file is not valid JSON: " + exception.Message, line, column, "Fix the JSON syntax; the input file cannot be read until it parses.")];
        }

        using (document)
        {
            var problems = new List<ScriptProblem>();
            if (schema is not null)
            {
                var result = schema.Evaluate(document.RootElement, Evaluation);
                if (!result.IsValid)
                {
                    problems.AddRange((result.Details ?? [])
                        .Where(static detail => detail.Errors is { Count: > 0 })
                        .SelectMany(static detail => detail.Errors!.Values.Select(error => (Location: detail.InstanceLocation.ToString(), Error: error)))
                        .DistinctBy(static item => item.Location + "|" + item.Error)
                        .Take(MaxSchemaProblems)
                        .Select(item => Problem(
                            path,
                            ScriptDiagnosticCodes.InputInvalid,
                            $"{(item.Location.Length == 0 ? "the file" : item.Location)}: {item.Error}",
                            line: null,
                            column: null,
                            "The input file does not match the engine's input schema. Fix the value at the location named in the message.")));
                }
            }

            foreach (var name in InputFileReferences.FindUndeclaredActions(text))
            {
                var (line, column) = FindActionUse(text, name);
                problems.Add(Problem(
                    path,
                    ScriptDiagnosticCodes.InputReferenceUnsupported,
                    $"the file uses the action '{name}' without declaring it",
                    line,
                    column,
                    "Every action an input file maps, chords or combines must be declared in the same file's actions; input files cannot share actions yet."));
            }

            return problems;
        }
    }

    private static (int? Line, int? Column) FindActionUse(string text, string name)
    {
        foreach (Match match in ActionUsePattern().Matches(text))
        {
            if (string.Equals(match.Groups["value"].Value, name, StringComparison.Ordinal))
            {
                var index = match.Groups["value"].Index;
                var lineStart = text.LastIndexOf('\n', Math.Max(0, index - 1)) + 1;
                return (1 + text.AsSpan(0, index).Count('\n'), index - lineStart + 1);
            }
        }

        return (null, null);
    }

    private static ScriptProblem Problem(string path, string code, string message, int? line, int? column, string explanation)
    {
        var location = line is { } at ? $"{path}:{at}:{column ?? 1}" : path;
        return new ScriptProblem(ScriptProblemSeverity.Error, code, message, path, line, column, $"error[{code}]: {message}\n --> {location}\n", explanation);
    }

    [GeneratedRegex("\"(?:action|chord_action)\"\\s*:\\s*\"(?<value>[^\"]*)\"", RegexOptions.CultureInvariant, matchTimeoutMilliseconds: 1000)]
    private static partial Regex ActionUsePattern();
}
