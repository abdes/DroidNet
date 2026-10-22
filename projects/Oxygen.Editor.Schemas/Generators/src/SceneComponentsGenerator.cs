// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using System.Text;
using Microsoft.CodeAnalysis;
using Microsoft.CodeAnalysis.Text;

namespace Oxygen.Editor.Schemas.Generators;

/// <summary>
/// Generates the editor's scene property vocabulary from the engine's scene components schema, given as the additional
/// file <c>SceneComponents.yaml</c>. The MSBuild property <c>SceneComponentsOutput</c> names what the project owns:
/// <c>WireIds</c> (the runtime's component and field ids), <c>PropertyIds</c> (the component kinds and typed property
/// ids) or <c>WireTable</c> (the live preview's property-to-field table), several separated by semicolons.
/// </summary>
/// <remarks>
/// The engine generates its own component and field ids from the same file, so both sides of the wire use one
/// declaration. Problems in the file are reported as errors at the entry they are about. The compiler reruns the
/// generator only when the file or the property changes.
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
        DependencyLoader.EnsureYamlLoaded();

        var outputs = context.AnalyzerConfigOptionsProvider
            .Select(static (options, _) => options.GlobalOptions.TryGetValue(OutputProperty, out var value) ? value : string.Empty);
        var files = context.AdditionalTextsProvider
            .Where(static file => string.Equals(Path.GetFileName(file.Path), SchemaFileName, StringComparison.OrdinalIgnoreCase))
            .Select(static (file, cancellation) => (file.Path, Text: file.GetText(cancellation)?.ToString() ?? string.Empty))
            .Collect();

        context.RegisterSourceOutput(outputs.Combine(files), static (output, input) => Generate(output, input.Left, input.Right));
    }

    private static void Generate(SourceProductionContext output, string requested, ImmutableArray<(string Path, string Text)> files)
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
        foreach (var diagnostic in diagnostics)
        {
            output.ReportDiagnostic(diagnostic);
        }

        if (schema is null)
        {
            return;
        }

        foreach (var name in names.Distinct(StringComparer.Ordinal))
        {
            var (hintName, source) = SceneComponentsEmitter.Render(name, schema);
            output.AddSource(hintName, SourceText.From(source, Encoding.UTF8));
        }
    }
}
