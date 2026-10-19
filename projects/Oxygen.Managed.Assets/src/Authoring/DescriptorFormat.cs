// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text;
using System.Text.Json;
using Json.Schema;
using Oxygen.Managed.Assets.Diagnostics;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Managed.Assets.Authoring;

/// <summary>What the readers of the engine's JSON descriptors share: parsing, the embedded engine schemas and problems.</summary>
internal static class DescriptorFormat
{
    private const string NameSchemaFileName = "oxygen.name.schema.json";

    /// <summary>Parses descriptor JSON as the cooker does, with comments and trailing commas.</summary>
    /// <param name="jsonUtf8">The descriptor bytes, with or without a UTF-8 byte order mark.</param>
    /// <param name="notJsonCode">The code of the problem the family reports for bytes that are not JSON.</param>
    /// <param name="message">What is wrong, for the user.</param>
    /// <returns>The parsed document.</returns>
    /// <exception cref="AssetFormatException">The bytes are not JSON; the problem has the line and column of the error.</exception>
    public static JsonDocument Parse(ReadOnlySpan<byte> jsonUtf8, string notJsonCode, string message)
    {
        try
        {
            return JsonDocument.Parse(WithoutByteOrderMark(jsonUtf8).ToArray(), new JsonDocumentOptions
            {
                AllowTrailingCommas = true,
                CommentHandling = JsonCommentHandling.Skip,
            });
        }
        catch (JsonException exception)
        {
            var problem = new AssetProblem(DiagnosticSeverity.Error, notJsonCode, message)
            {
                Line = (int?)(exception.LineNumber + 1),
                Column = (int?)(exception.BytePositionInLine + 1),
                Explanation = exception.Message,
            };
            throw new AssetFormatException(problem, exception);
        }
    }

    /// <summary>Removes a UTF-8 byte order mark, which the cooker accepts.</summary>
    /// <param name="jsonUtf8">The descriptor bytes.</param>
    /// <returns>The bytes after the byte order mark, if any.</returns>
    public static ReadOnlySpan<byte> WithoutByteOrderMark(ReadOnlySpan<byte> jsonUtf8)
        => jsonUtf8.StartsWith(Encoding.UTF8.Preamble) ? jsonUtf8[Encoding.UTF8.Preamble.Length..] : jsonUtf8;

    /// <summary>Loads an engine schema embedded in this assembly, with the shared name definitions it references.</summary>
    /// <param name="fileName">The schema's file name.</param>
    /// <returns>The schema.</returns>
    public static JsonSchema LoadSchema(string fileName)
    {
        var options = new BuildOptions();
        options.SchemaRegistry.Fetch = static (uri, _) =>
            string.Equals(Path.GetFileName(uri.LocalPath), NameSchemaFileName, StringComparison.Ordinal)
                ? JsonSchema.FromText(ReadSchemaResource(NameSchemaFileName))
                : null;
        return JsonSchema.FromText(ReadSchemaResource(fileName), options);
    }

    /// <summary>Describes the errors of a failed schema evaluation, one per line.</summary>
    /// <param name="evaluation">The evaluation, in list output format.</param>
    /// <returns>The errors.</returns>
    public static string Describe(EvaluationResults evaluation)
        => string.Join(Environment.NewLine, Issues(evaluation).Select(static issue => $"{issue.Location}: {issue.Error}"));

    /// <summary>Lists the errors of a failed schema evaluation with the JSON pointer of the value each is about.</summary>
    /// <param name="evaluation">The evaluation, in list output format.</param>
    /// <returns>The errors, each with its value's pointer; empty for the document itself.</returns>
    public static IEnumerable<(string Location, string Error)> Issues(EvaluationResults evaluation)
    {
        foreach (var (keyword, error) in evaluation.Errors ?? new Dictionary<string, string>(StringComparer.Ordinal))
        {
            yield return (evaluation.InstanceLocation.ToString(), $"{keyword} {error}");
        }

        foreach (var detail in evaluation.Details ?? [])
        {
            foreach (var issue in Issues(detail))
            {
                yield return issue;
            }
        }
    }

    /// <summary>Creates the exception for an error problem.</summary>
    /// <param name="code">The problem's code.</param>
    /// <param name="message">What is wrong, for the user.</param>
    /// <param name="explanation">The technical detail, if any.</param>
    /// <returns>The exception to throw.</returns>
    public static AssetFormatException Problem(string code, string message, string? explanation = null)
        => new(new AssetProblem(DiagnosticSeverity.Error, code, message) { Explanation = explanation });

    private static string ReadSchemaResource(string fileName)
    {
        using var stream = typeof(DescriptorFormat).Assembly.GetManifestResourceStream(fileName)
            ?? throw new InvalidOperationException($"The engine schema '{fileName}' is missing.");
        using var reader = new StreamReader(stream);
        return reader.ReadToEnd();
    }
}
