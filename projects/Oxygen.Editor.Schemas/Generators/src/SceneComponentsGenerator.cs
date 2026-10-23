// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using System.Globalization;
using System.Text;
using Microsoft.CodeAnalysis;
using Microsoft.CodeAnalysis.Text;

namespace Oxygen.Editor.Schemas.Generators;

/// <summary>
/// Generates the editor's scene property vocabulary from the engine's scene components schema, given as the additional
/// file <c>SceneComponents.yaml</c>. The MSBuild property <c>SceneComponentsOutput</c> names what the project owns:
/// <c>WireIds</c> (the runtime's component, field and override ids), <c>PropertyIds</c> (the component kinds, typed
/// property ids, defaults, ranges and units) or <c>WireTable</c> (the live preview's property-to-field table), several
/// separated by semicolons.
/// </summary>
/// <remarks>
/// The engine generates its own code from the same file, so both sides of the wire use one declaration. Problems in the
/// file are reported as errors at the entry they are about, and so is an enumeration whose editor type, when the
/// compilation sees it, has other members. The compiler reruns the generator only when the file, the property or those
/// members change.
/// </remarks>
[Generator]
public sealed class SceneComponentsGenerator : IIncrementalGenerator
{
    /// <summary>The file name of the schema.</summary>
    public const string SchemaFileName = "SceneComponents.yaml";

    private const string OutputProperty = "build_property.SceneComponentsOutput";

    /// <inheritdoc/>
    public void Initialize(IncrementalGeneratorInitializationContext context)
    {
        var outputs = context.AnalyzerConfigOptionsProvider
            .Select(static (options, _) => options.GlobalOptions.TryGetValue(OutputProperty, out var value) ? value : string.Empty);
        var files = context.AdditionalTextsProvider
            .Where(static file => string.Equals(Path.GetFileName(file.Path), SchemaFileName, StringComparison.OrdinalIgnoreCase))
            .Select(static (file, cancellation) => (file.Path, Text: file.GetText(cancellation)?.ToString() ?? string.Empty))
            .Collect();
        var enums = context.CompilationProvider.Select(static (compilation, _) => EditorEnumMembers(compilation));

        context.RegisterSourceOutput(
            outputs.Combine(files).Combine(enums),
            static (output, input) => Generate(output, input.Left.Left, input.Left.Right, input.Right));
    }

    // The members of each mapped editor enumeration the compilation sees, as "Enumeration:Name=Value,Name=Value"
    // separated by semicolons. A string, so the pipeline reruns only when the members change.
    private static string EditorEnumMembers(Compilation compilation)
    {
        var lines = new List<string>();
        foreach (var name in SceneComponentsTypes.EnumNames.OrderBy(static name => name, StringComparer.Ordinal))
        {
            var type = compilation.GetTypeByMetadataName(SceneComponentsTypes.EnumType(name)!.Replace("global::", string.Empty));
            if (type is { TypeKind: TypeKind.Enum })
            {
                lines.Add(name + ":" + Signature(type.GetMembers().OfType<IFieldSymbol>().Where(static field => field.HasConstantValue)
                    .Select(static field => (field.Name, Value: Convert.ToInt64(field.ConstantValue, CultureInfo.InvariantCulture)))));
            }
        }

        return string.Join(";", lines);
    }

    private static string Signature(IEnumerable<(string Name, long Value)> members)
        => string.Join(",", members.OrderBy(static member => member.Value).ThenBy(static member => member.Name, StringComparer.Ordinal)
            .Select(static member => member.Name + "=" + member.Value.ToString(CultureInfo.InvariantCulture)));

    private static void Generate(SourceProductionContext output, string requested, ImmutableArray<(string Path, string Text)> files, string enums)
    {
        var names = requested.Split([';'], StringSplitOptions.RemoveEmptyEntries).Select(static name => name.Trim()).ToList();
        var unknown = names.Where(static name => !SceneComponentsEmitter.Outputs.Contains(name, StringComparer.Ordinal)).ToList();
        if (names.Count == 0 || unknown.Count != 0)
        {
            output.ReportDiagnostic(Diagnostic.Create(
                SceneComponentsDiagnostics.UnknownOutput,
                Location.None,
                names.Count == 0 ? requested : string.Join(";", unknown),
                string.Join(", ", SceneComponentsEmitter.Outputs)));
            return;
        }

        if (files.Length != 1)
        {
            output.ReportDiagnostic(Diagnostic.Create(SceneComponentsDiagnostics.SchemaFileCount, Location.None, files.Length));
            return;
        }

        var diagnostics = new List<Diagnostic>();
        var schema = SceneComponentsReader.Read(files[0].Path, files[0].Text, diagnostics);
        if (schema is not null)
        {
            CheckEnums(schema, enums, diagnostics);
        }

        foreach (var diagnostic in diagnostics)
        {
            output.ReportDiagnostic(diagnostic);
        }

        if (schema is null || diagnostics.Count != 0)
        {
            return;
        }

        foreach (var name in names.Distinct(StringComparer.Ordinal))
        {
            var (hintName, source) = SceneComponentsEmitter.Render(name, schema);
            output.AddSource(hintName, SourceText.From(source, Encoding.UTF8));
        }
    }

    private static void CheckEnums(SceneComponentsSchema schema, string enums, List<Diagnostic> diagnostics)
    {
        var editorMembers = enums.Split([';'], StringSplitOptions.RemoveEmptyEntries)
            .Select(static line => line.Split([':'], 2))
            .ToDictionary(static parts => parts[0], static parts => parts[1], StringComparer.Ordinal);
        foreach (var entry in schema.Enums)
        {
            if (!editorMembers.TryGetValue(entry.Name, out var editor))
            {
                continue;
            }

            var declared = Signature(entry.Members.Select(static member => (member.Name, (long)member.Value)));
            if (!string.Equals(declared, editor, StringComparison.Ordinal))
            {
                diagnostics.Add(Diagnostic.Create(
                    SceneComponentsDiagnostics.EnumMismatch,
                    entry.Locations.Of("members"),
                    entry.Name,
                    $"the schema declares {declared} but {SceneComponentsTypes.EnumType(entry.Name)} has {editor}"));
            }
        }
    }
}
