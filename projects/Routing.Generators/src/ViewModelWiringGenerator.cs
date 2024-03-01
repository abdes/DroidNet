// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing.Generators;

using System.Diagnostics;
using System.Reflection;
using System.Text;
using DroidNet.Routing.Generators.Templates;
using Microsoft.CodeAnalysis;
using Microsoft.CodeAnalysis.CSharp;
using Microsoft.CodeAnalysis.CSharp.Syntax;
using Microsoft.CodeAnalysis.Text;

/*
 * TODO(abdes): migrate to Incremental Source Generator
 *
 * How to se tup source generator project
 * https://stackoverflow.com/questions/74915263/c-sharp-source-generator-filenotfoundexception-could-not-load-file-or-assembly
 *
 * How to debug source generators:
 * https://github.com/dotnet/roslyn-sdk/issues/850
 *
 * How to enable Syntax Visualizer:
 * https://learn.microsoft.com/en-us/dotnet/csharp/roslyn-sdk/syntax-visualizer?tabs=csharp
 */

/// <summary>
/// A C# source generator that augments classes decorated with the
/// <see cref="ViewModelAttribute" /> attribute.
/// </summary>
/// <remarks>
/// <para>
/// A source generator is simply a class decorated with the
/// <see cref="GeneratorAttribute" />
/// which implements the <see cref="ISourceGenerator" /> interface.
/// </para>
/// <para>
/// Our strategy for the generator is to use <see cref="ISyntaxReceiver" /> to
/// analyze the stream of syntax nodes. We'll only get interested if the syntax
/// node is a class declaration decorated with our target attribute.
/// </para>
/// <para>
/// The generated source code use templates with manual substitution, simply
/// because pulling an external dependency in source generator project just for
/// such a simple task will introduce many issues with the tooling.
/// </para>
/// </remarks>
[Generator]
public class ViewModelWiringGenerator : ISourceGenerator
{
    private const string ViewForTemplate = "ViewForTemplate.txt";

    private static readonly DiagnosticDescriptor TemplateLoadError = new(
        "VMWGEN001",
        "Missing template",
        "Code generation failed. {0}.",
        "Package",
        DiagnosticSeverity.Error,
        description: "The generator package should include templates as embedded resources.",
        isEnabledByDefault: true);

    private static readonly DiagnosticDescriptor AttributeAnnotationError = new(
        "VMWGEN002",
        "Invalid attribute annotation",
        $"Invalid attribute declaration. Use [{nameof(ViewModelAttribute)}(typeof(T)] where T is the Viewmodel class.",
        "Usage",
        DiagnosticSeverity.Error,
        description: "View classes should be annotated with a valid `ViewModel` attribute with the ViewModel type.",
        isEnabledByDefault: true);

    private static readonly DiagnosticDescriptor ViewModelNotFoundError = new(
        "VMWGEN003",
        "ViewModel not found",
        "Could not find ViewModel symbol from identifier '{0}'",
        "Usage",
        DiagnosticSeverity.Error,
        description: "The ViewModel type in the attribute annotation should be a valid type.",
        isEnabledByDefault: true);

    /// <summary>
    /// Called before generation occurs. This generator uses the
    /// <paramref name="context" />
    /// to register a callback that initializes an
    /// <see cref="AttributeSyntaxReceiver{TAttribute}" />
    /// for the <see cref="ViewModelAttribute" />.
    /// </summary>
    /// <param name="context">
    /// The <see cref="GeneratorInitializationContext" /> to
    /// register callbacks on.
    /// </param>
    public void Initialize(GeneratorInitializationContext context)
        => context.RegisterForSyntaxNotifications(() => new AttributeSyntaxReceiver<ViewModelAttribute>());

    /// <summary>
    /// Called to perform source generation. A generator can use the
    /// <paramref name="context" />
    /// to add source files via the
    /// <see cref="GeneratorExecutionContext.AddSource(string, SourceText)" />
    /// method.
    /// </summary>
    /// <param name="context">
    /// The <see cref="GeneratorExecutionContext" /> to add
    /// source to.
    /// </param>
    public void Execute(GeneratorExecutionContext context)
    {
        if (context.SyntaxReceiver is not AttributeSyntaxReceiver<ViewModelAttribute> syntaxReceiver)
        {
            return;
        }

        foreach (var classSyntax in syntaxReceiver.Classes)
        {
            // Converting the class to semantic model to access much more meaningful data.
            var model = context.Compilation.GetSemanticModel(classSyntax.SyntaxTree);

            // Parse to declared symbol, so you can access each part of code separately,
            // such as interfaces, methods, members, constructor parameters etc.
            var viewSymbol = model.GetDeclaredSymbol(classSyntax);
            Debug.Assert(viewSymbol != null, nameof(viewSymbol) + " != null");

            // Finding my GenerateServiceAttribute over it. I'm sure this attribute is placed, because my syntax receiver already checked before.
            // So, I can surely execute following query.
            var attribute = classSyntax.AttributeLists.SelectMany(sm => sm.Attributes)
                .First(
                    x => x.Name.ToString()
                        .EnsureEndsWith("Attribute")
                        .Equals(nameof(ViewModelAttribute), StringComparison.Ordinal));

            // Getting constructor parameter of the attribute. It might be not presented.
            var viewModelParameter = attribute.ArgumentList?.Arguments.FirstOrDefault();
            Debug.Assert(viewModelParameter != null, nameof(viewModelParameter) + " != null");

            // The parameter is of the form `typeof(ViewModelClass)`.
            // We are just interested in the `ViewModelClass` identifier.
            if (viewModelParameter!.Expression.DescendantNodes()
                    .FirstOrDefault(x => x.IsKind(SyntaxKind.IdentifierName)) is not IdentifierNameSyntax identifier)
            {
                context.ReportDiagnostic(Diagnostic.Create(AttributeAnnotationError, attribute.GetLocation()));
                continue;
            }

            var viewModelSymbol = model.GetSymbolInfo(identifier).Symbol;
            if (viewModelSymbol is null)
            {
                context.ReportDiagnostic(
                    Diagnostic.Create(ViewModelNotFoundError, attribute.GetLocation(), identifier.ToString()));
                continue;
            }

            var templateParameters = new ViewForTemplateParameters(
                viewSymbol!.Name,
                viewModelSymbol.Name,
                GetNamespaceRecursively(viewSymbol.ContainingNamespace),
                viewModelSymbol.ContainingNamespace.ToString());

            try
            {
                var sourceCode = GetSourceCodeFor(templateParameters);
                context.AddSource(
                    $"{templateParameters.ViewClassName}.g.cs",
                    SourceText.From(sourceCode, Encoding.UTF8));
            }
            catch (Exception ex)
            {
                context.ReportDiagnostic(Diagnostic.Create(TemplateLoadError, attribute.GetLocation(), ex.Message));
                return;
            }
        }
    }

    private static string GetEmbeddedResource(string path)
    {
        var assembly = Assembly.GetExecutingAssembly();
        List<string> resourceNames = [.. assembly.GetManifestResourceNames()];

        var dotPath = path.Replace("/", ".");
        dotPath = resourceNames.FirstOrDefault(r => r.Contains(dotPath)) ?? throw new MissingTemplateException(path);

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

    private static string GetNamespaceRecursively(ISymbol symbol) => symbol.ContainingNamespace == null
        ? symbol.Name
        : (GetNamespaceRecursively(symbol.ContainingNamespace) + "." + symbol.Name).Trim('.');

    private sealed class AttributeSyntaxReceiver<TAttribute> : ISyntaxReceiver
    {
        /// <summary>
        /// Gets the list of classes that need to be augmented (i.e. decorated
        /// with the <typeparamref name="TAttribute" /> name="TAttribute" />).
        /// </summary>
        public List<ClassDeclarationSyntax> Classes { get; } = [];

        public void OnVisitSyntaxNode(SyntaxNode syntaxNode)
        {
            if (syntaxNode is ClassDeclarationSyntax { AttributeLists.Count: > 0 } classDeclarationSyntax &&
                classDeclarationSyntax.AttributeLists.Any(
                    al => al.Attributes.Any(
                        a => a.Name.ToString()
                            .EnsureEndsWith("Attribute")
                            .Equals(typeof(TAttribute).Name, StringComparison.Ordinal))))
            {
                this.Classes.Add(classDeclarationSyntax);
            }
        }
    }

    /// <summary>
    /// Internal exception, thrown to terminate the generation when the template for the class to be generated could not be found.
    /// </summary>
    /// <param name="path">The specified path for the missing template.</param>
    private sealed class MissingTemplateException(string path) : ApplicationException($"Missing template '{path}'");

    /// <summary>
    /// Internal exception, thrown to terminate the generation when the template for the class to be generated is invalid.
    /// </summary>
    /// <param name="path">The template path.</param>
    private sealed class InvalidTemplateException(string path) : ApplicationException($"Invalid template '{path}'");

    /// <summary>
    /// Internal exception, thrown to terminate the generation when the template for the class to be generated was found but
    /// could not be loaded.
    /// </summary>
    /// <param name="path">The template path.</param>
    /// <param name="error">A description of the error encountered while trying to load the template.</param>
    private sealed class CouldNotLoadTemplateException(string path, string error) : Exception(
        $"Could not load template '{path}': {error}");
}

/// <summary>
/// A simple extension method that will allow us to generically compare the
/// attribute name token on the class declaration syntax node with our target
/// attribute type.
/// </summary>
internal static class StringExtensions
{
    public static string EnsureEndsWith(this string source, string suffix)
    {
        if (source.EndsWith(suffix, StringComparison.InvariantCulture))
        {
            return source;
        }

        return source + suffix;
    }
}
