// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json;
using Json.Schema;
using Oxygen.Managed.Assets.Diagnostics;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Managed.Assets.Authoring.Input;

/// <summary>Reads the engine's input source, <c>*.input.json</c>, with the position of every declaration.</summary>
public static class InputSourceReader
{
    /// <summary>The most schema problems <see cref="Check"/> reports for one file.</summary>
    public const int MaxSchemaProblems = 10;

    private static readonly Lazy<JsonSchema> Schema = new(static () => DescriptorFormat.LoadSchema("oxygen.input.schema.json"));

    /// <summary>Reads an input source.</summary>
    /// <param name="jsonUtf8">The source bytes.</param>
    /// <returns>The source.</returns>
    /// <exception cref="AssetFormatException">
    /// The content is not JSON or does not match the engine's input schema: its problem has one of the
    /// <see cref="InputSourceCodes"/>.
    /// </exception>
    public static InputSource Read(ReadOnlySpan<byte> jsonUtf8)
    {
        var report = Check(jsonUtf8);
        return report.Source ?? throw new AssetFormatException(report.Problems[0]);
    }

    /// <summary>
    /// Checks an input source as the cooker will: JSON, the engine's input schema, one declaration per name, and every
    /// action named by its name declared in the source. A reference by virtual path is resolved by its consumer, which
    /// knows the project.
    /// </summary>
    /// <param name="jsonUtf8">The source bytes.</param>
    /// <returns>The source, when it is JSON that matches the schema, and every problem with its position.</returns>
    public static InputSourceReport Check(ReadOnlySpan<byte> jsonUtf8)
    {
        JsonDocument document;
        try
        {
            document = DescriptorFormat.Parse(jsonUtf8, InputSourceCodes.NotJson, "The input source is not valid JSON.");
        }
        catch (AssetFormatException exception)
        {
            return new InputSourceReport(Source: null, [exception.Problem]);
        }

        using (document)
        {
            var positions = JsonPositions.Index(DescriptorFormat.WithoutByteOrderMark(jsonUtf8));
            var evaluation = Schema.Value.Evaluate(document.RootElement, new EvaluationOptions { OutputFormat = OutputFormat.List });
            if (!evaluation.IsValid)
            {
                return new InputSourceReport(Source: null, SchemaProblems(evaluation, positions));
            }

            var source = ReadSource(document.RootElement, positions);
            return new InputSourceReport(source, [.. DeclarationProblems(source)]);
        }
    }

    private static List<AssetProblem> SchemaProblems(EvaluationResults evaluation, JsonPositions positions)
        => [.. DescriptorFormat.Issues(evaluation)
            .Distinct()
            .Take(MaxSchemaProblems)
            .Select(issue => Problem(
                InputSourceCodes.SchemaMismatch,
                $"{(issue.Location.Length == 0 ? "The input source" : issue.Location)}: {issue.Error}",
                positions.Find(issue.Location)))];

    private static InputSource ReadSource(JsonElement root, JsonPositions positions)
        => new()
        {
            Actions = [.. Items(root, "actions").Select((action, index) => ReadAction(action, $"/actions/{index}", positions))],
            Contexts = [.. Items(root, "contexts").Select((context, index) => ReadContext(context, $"/contexts/{index}", positions))],
        };

    private static InputActionDeclaration ReadAction(JsonElement action, string pointer, JsonPositions positions)
        => new(action.GetProperty("name").GetString()!, action.GetProperty("type").GetString()!)
        {
            ConsumesInput = action.TryGetProperty("consumes_input", out var consumes) && consumes.GetBoolean(),
            Accumulation = action.TryGetProperty("accumulation", out var accumulation) ? accumulation.GetString()! : "highest_absolute",
            Position = positions.Find(pointer + "/name"),
        };

    private static InputContextDeclaration ReadContext(JsonElement context, string pointer, JsonPositions positions)
    {
        var references = new List<InputActionReference>();
        foreach (var (mapping, index) in Items(context, "mappings").Select(static (mapping, index) => (mapping, index)))
        {
            var mappingPointer = $"{pointer}/mappings/{index}";
            Add(references, mapping, "action", mappingPointer, positions);
            foreach (var (trigger, triggerIndex) in Items(mapping, "triggers").Select(static (trigger, index) => (trigger, index)))
            {
                var triggerPointer = $"{mappingPointer}/triggers/{triggerIndex}";
                Add(references, trigger, "chord_action", triggerPointer, positions);
                foreach (var list in (string[])["combo_actions", "aux"])
                {
                    foreach (var (step, stepIndex) in Items(trigger, list).Select(static (step, index) => (step, index)))
                    {
                        Add(references, step, "action", $"{triggerPointer}/{list}/{stepIndex}", positions);
                    }
                }
            }
        }

        return new InputContextDeclaration(context.GetProperty("name").GetString()!)
        {
            Priority = context.TryGetProperty("priority", out var priority) ? priority.GetInt32() : 0,
            Position = positions.Find(pointer + "/name"),
            ActionReferences = references,
        };
    }

    private static IEnumerable<AssetProblem> DeclarationProblems(InputSource source)
    {
        foreach (var action in Duplicates(source.Actions, static action => action.Name))
        {
            yield return Problem(InputSourceCodes.ActionDuplicate, $"The action '{action.Name}' is declared more than once.", action.Position);
        }

        foreach (var context in Duplicates(source.Contexts, static context => context.Name))
        {
            yield return Problem(InputSourceCodes.ContextDuplicate, $"The mapping context '{context.Name}' is declared more than once.", context.Position);
        }

        foreach (var reference in source.ActionReferences.Where(reference => !reference.IsPath && source.FindAction(reference.Text) is null))
        {
            yield return Problem(
                InputSourceCodes.ActionUndeclared,
                $"The source declares no action '{reference.Text}'. Declare it, or name an action of another input source by its virtual path.",
                reference.Position);
        }
    }

    private static IEnumerable<T> Duplicates<T>(IEnumerable<T> items, Func<T, string> name)
        => items.GroupBy(name, StringComparer.Ordinal).SelectMany(static group => group.Skip(1));

    private static void Add(List<InputActionReference> references, JsonElement parent, string property, string pointer, JsonPositions positions)
    {
        if (parent.TryGetProperty(property, out var value))
        {
            references.Add(new InputActionReference(value.GetString()!) { Position = positions.Find($"{pointer}/{property}") });
        }
    }

    private static IEnumerable<JsonElement> Items(JsonElement parent, string property)
        => parent.TryGetProperty(property, out var items) ? items.EnumerateArray() : [];

    private static AssetProblem Problem(string code, string message, SourcePosition? position)
        => new(DiagnosticSeverity.Error, code, message) { Line = position?.Line, Column = position?.Column };
}
