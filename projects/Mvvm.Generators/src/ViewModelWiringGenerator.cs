// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Mvvm.Generators;

using System.Diagnostics;
using System.Reflection;
using System.Text;
using DroidNet.Mvvm.Generators.Templates;
using Microsoft.CodeAnalysis;
using Microsoft.CodeAnalysis.Text;

/*
 * How to se tup source generator project
 * https://stackoverflow.com/questions/74915263/c-sharp-source-generator-filenotfoundexception-could-not-load-file-or-assembly
 *
 * How to debug source generators:
 * https://github.com/dotnet/roslyn-sdk/issues/850
 *
 * How to enable Syntax Visualizer:
 * https://learn.microsoft.com/en-us/dotnet/csharp/roslyn-sdk/syntax-visualizer?tabs=csharp
 *
 * How to test:
 * https://andrewlock.net/creating-a-source-generator-part-2-testing-an-incremental-generator-with-snapshot-testing/
 */

/// <summary>
/// A C# source generator that augments classes decorated with the <see cref="ViewModelAttribute" /> attribute.
/// </summary>
/// <remarks>
/// The generated source code use templates with manual substitution, simply because pulling an external dependency in
/// source generator project just for such a simple task will introduce many issues with the tooling.
/// </remarks>
[Generator(LanguageNames.CSharp)]
public sealed class ViewModelWiringGenerator : IIncrementalGenerator
{
    /// <summary>The name of the template file.</summary>
    /// <remarks>!!! The template file in the project must be marked as "Embedded Resource".</remarks>
    private const string ViewForTemplate = "ViewForTemplate.txt";

    private static readonly DiagnosticDescriptor TemplateLoadError = new(
        "VMWGEN001",
        "Missing template",
        "Code generation failed. {0}.",
        "Package",
        DiagnosticSeverity.Error,
        isEnabledByDefault: true,
        description: "The generator package should include templates as embedded resources.");

    private static readonly DiagnosticDescriptor AttributeAnnotationError = new(
        "VMWGEN002",
        "Invalid attribute annotation",
        "Invalid attribute declaration. Use [ViewModel(typeof(T)] where T is the Viewmodel class.",
        "Usage",
        DiagnosticSeverity.Error,
        isEnabledByDefault: true,
        description: "View classes should be annotated with a valid `ViewModel` attribute with the ViewModel type.");

    /// <summary>
    /// Called by the host, before generation occurs, exactly once, regardless of the number of further compilations that may
    /// occur. For instance a host with multiple loaded projects may share the same generator instance across multiple projects,
    /// and will only call Initialize a single time for the lifetime of the host.
    /// </summary>
    /// <param name="context">
    /// The context used by the generator to define a set of transformations that represent the execution pipeline. The defined
    /// transformations are not executed directly at initialization, and instead are deferred until the data they are using
    /// changes.
    /// </param>
    public void Initialize(IncrementalGeneratorInitializationContext context)
    {
        var viewClasses = context.SyntaxProvider.ForAttributeWithMetadataName(
            "DroidNet.Mvvm.Generators.ViewModelAttribute",
            (_, _) => true,
            (syntaxContext, _) => syntaxContext);

        context.RegisterSourceOutput(viewClasses, GenerateViewModelWiring);
    }

    private static void GenerateViewModelWiring(SourceProductionContext context, GeneratorAttributeSyntaxContext syntax)
    {
        // If we are called, there must be a ViewModelAttribute in the attributes.
        var attribute = syntax.Attributes[0];

        var viewClassName = syntax.TargetSymbol.Name;
        var viewNameSpace = syntax.TargetSymbol.ContainingNamespace.ToString();

        if (attribute.ConstructorArguments.IsEmpty)
        {
            context.ReportDiagnostic(Diagnostic.Create(AttributeAnnotationError, syntax.TargetNode.GetLocation()));
            return;
        }

        if (attribute.ConstructorArguments[0].Value is not INamedTypeSymbol viewModelType)
        {
            context.ReportDiagnostic(Diagnostic.Create(AttributeAnnotationError, syntax.TargetNode.GetLocation()));
            return;
        }

        var viewModelClassName = viewModelType.OriginalDefinition.Name;
        var viewModelNameSpace = viewModelType.ContainingNamespace.ToString();

        var templateParameters = new ViewForTemplateParameters(
            viewClassName,
            viewModelClassName,
            viewNameSpace,
            viewModelNameSpace);

        try
        {
            var sourceCode = GetSourceCodeFor(templateParameters);
            context.AddSource(
                $"{templateParameters.ViewClassName}.g.cs",
                SourceText.From(sourceCode, Encoding.UTF8));
        }
        catch (Exception ex)
        {
            context.ReportDiagnostic(Diagnostic.Create(TemplateLoadError, syntax.TargetNode.GetLocation(), ex.Message));
        }
    }

    private static string GetEmbeddedResource(string path)
    {
        var assembly = Assembly.GetExecutingAssembly();
        List<string> resourceNames = [.. assembly.GetManifestResourceNames()];

        var dotPath = path.Replace('/', '.');
        dotPath = resourceNames.Find(r => r.Contains(dotPath)) ?? throw new MissingTemplateException(path);

        try
        {
            using var stream = assembly.GetManifestResourceStream(dotPath);
            Debug.Assert(stream is not null, $"expecting to be able to load the template as a resource at: {dotPath}");
            return new StreamReader(stream).ReadToEnd();
        }
        catch (Exception ex)
        {
            throw new CouldNotLoadTemplateException(path, ex.ToString());
        }
    }

    private static string GetSourceCodeFor(ViewForTemplateParameters parameters)
    {
        var template = GetEmbeddedResource(ViewForTemplate);

        var tree = HandlebarsDotNet.Handlebars.Compile(template);
        var sourceCode = tree(parameters);
        return sourceCode ?? throw new InvalidTemplateException(ViewForTemplate);
    }

    // These are internal exceptions only with their custom constructors, so that we have a common code block for diagnostics.
#pragma warning disable RCS1194 // Implement exception constructors

    /// <summary>
    /// Internal exception, thrown to terminate the generation when the template for the class to be generated could not be found.
    /// </summary>
    /// <param name="path">The specified path for the missing template.</param>
    private sealed class MissingTemplateException(string path) : Exception($"Missing template '{path}'");

    /// <summary>
    /// Internal exception, thrown to terminate the generation when the template for the class to be generated is invalid.
    /// </summary>
    /// <param name="path">The template path.</param>
    private sealed class InvalidTemplateException(string path) : Exception($"Invalid template '{path}'");

    /// <summary>
    /// Internal exception, thrown to terminate the generation when the template for the class to be generated was found but
    /// could not be loaded.
    /// </summary>
    /// <param name="path">The template path.</param>
    /// <param name="error">A description of the error encountered while trying to load the template.</param>
    private sealed class CouldNotLoadTemplateException(string path, string error) : Exception(
        $"Could not load template '{path}': {error}");

#pragma warning restore RCS1194 // Implement exception constructors
}
