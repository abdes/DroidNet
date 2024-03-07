// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Hosting.Generators;

using System;
using System.Collections.Immutable;
using System.Text;
using DroidNet.Hosting.Generators.Utils;
using Microsoft.CodeAnalysis;
using Microsoft.CodeAnalysis.Text;
using Microsoft.Extensions.DependencyInjection;

/// <summary>
/// Incremental generator for automatically adding services annotated with the <see cref="InjectAsAttribute" /> to the Dependency
/// Injector service collection.
/// </summary>
[Generator]
public class AutoInjectGenerator : IIncrementalGenerator
{
    private const string InjectAsAttributeName = "DroidNet.Hosting.Generators.InjectAsAttribute";

    private static readonly DiagnosticDescriptor AnnotationError = new(
        "INJECTAS001",
        "Invalid Annotation",
        "Code generation for InjectAs annotation failed. {0}.",
        "Usage",
        DiagnosticSeverity.Error,
        true);

    /// <summary>
    /// Initializes the generator by setting up the incremental pipeline.
    /// </summary>
    /// <param name="context">The initialization context.</param>
    public void Initialize(IncrementalGeneratorInitializationContext context)
    {
        var injections = context.SyntaxProvider
            .ForAttributeWithMetadataName(
                InjectAsAttributeName,
                (_, _) => true,
                (syntaxContext, _) => syntaxContext)
            .Collect();

        context.RegisterSourceOutput(injections, GenerateSource);
    }

    /// <summary>Generates the source code for the AutoInject extension method.</summary>
    /// <param name="spc">The source production context.</param>
    /// <param name="injectionContexts">The collection of attribute syntax contexts for which code needs to be generated.</param>
    private static void GenerateSource(
        SourceProductionContext spc,
        ImmutableArray<GeneratorAttributeSyntaxContext> injectionContexts)
    {
        var source = new StringBuilder(
            """
            // Distributed under the MIT License. See accompanying file LICENSE or copy
            // at https://opensource.org/licenses/MIT.
            // SPDX-License-Identifier: MIT

            namespace DroidNet.Hosting.Generators;

            using Microsoft.Extensions.DependencyInjection;

            public static class AutoInjectExtensions
            {
                public static IServiceCollection UseAutoInject(this IServiceCollection services)
                {

            """);

        foreach (var injectionContext in injectionContexts)
        {
            var injections = injectionContext.TargetSymbol.GetAttributes()
                .Where(a => a.AttributeClass?.Name == nameof(InjectAsAttribute));
            foreach (var attribute in injections)
            {
                try
                {
                    GenerateSourceForInjection(spc, injectionContext, attribute, source);
                }
                catch (Exception ex)
                {
                    spc.ReportDiagnostic(
                        Diagnostic.Create(AnnotationError, injectionContext.TargetNode.GetLocation(), ex.Message));
                }
            }
        }

        _ = source.AppendLine(
            """

                    return services;
                }
            }
            """);

        spc.AddSource("AutoInject.Extensions.g.cs", SourceText.From(source.ToString(), Encoding.UTF8));
    }

    private static void GenerateSourceForInjection(
        SourceProductionContext spc,
        GeneratorAttributeSyntaxContext injectionContext,
        AttributeData attribute,
        StringBuilder source)
    {
        var targetSymbol = injectionContext.TargetSymbol as INamedTypeSymbol ??
                           throw new InvalidAnnotationException(
                               $"annotation was placed on a symbol that is not a named type: {injectionContext.TargetSymbol}");

        var lifetimeValue = attribute.ConstructorArguments[0].Value;
        if (lifetimeValue is not int lifetimeUnboxed || !Enum.IsDefined(typeof(ServiceLifetime), lifetimeValue))
        {
            throw new InvalidAnnotationException($"did not recognize the specified lifetime `{lifetimeValue}`");
        }

        var lifetime = (ServiceLifetime)lifetimeUnboxed;

        var methodName = GetMethodName(lifetime);

        var targetIsInterface = targetSymbol.TypeKind == TypeKind.Interface;
        var serviceType = targetSymbol.ToDisplayString();

        var implTypeArg = attribute.NamedArguments
            .FirstOrDefault(a => a.Key == nameof(InjectAsAttribute.ImplementationType))
            .Value;

        string? implTypeFullName = null;
        if (targetIsInterface)
        {
            if (implTypeArg.IsNull || implTypeArg.Value is not ITypeSymbol implTypeSymbol)
            {
                throw new InvalidAnnotationException(
                    $"annotation placed on a interface type `{serviceType}` without specifying an implementation type");
            }

            if (!implTypeSymbol.ImplementsInterface(serviceType))
            {
                spc.ReportDiagnostic(
                    Diagnostic.Create(
                        AnnotationError,
                        injectionContext.TargetNode.GetLocation(),
                        $"The implementation type `{implTypeSymbol}` does not implement the annotated interface `{serviceType}`."));
                return;
            }

            implTypeFullName = implTypeSymbol.ToDisplayString();
        }

        string? key = null;
        var keyArg = attribute.NamedArguments
            .FirstOrDefault(a => a.Key == nameof(InjectAsAttribute.Key))
            .Value;
        if (keyArg is { IsNull: false, Value: not null })
        {
            key = keyArg.Value.ToString();
            methodName = methodName.Replace("Add", "AddKeyed");
        }

        var methodCallSegment = $"services.{methodName}{(targetIsInterface ? $"<{serviceType}>" : string.Empty)}";
        var keyParamSegment = key is null ? string.Empty : $"\"{key}\", ";
        var factorySegment
            = $"sp => ActivatorUtilities.CreateInstance<{(targetIsInterface ? implTypeFullName : serviceType)}>(sp)";
        var factorySegmentWithKey
            = $"(sp, _) => ActivatorUtilities.CreateInstance<{(targetIsInterface ? implTypeFullName : serviceType)}>(sp)";

        _ = source.AppendLine(
            $"        _ = {methodCallSegment}({keyParamSegment}{(key is null ? factorySegment : factorySegmentWithKey)});");
    }

    /// <summary>Gets the method name for the dependency injection based on the specified service lifetime.</summary>
    /// <param name="lifetime">The service lifetime.</param>
    /// <returns>The method name for the dependency injection.</returns>
    private static string GetMethodName(ServiceLifetime? lifetime)
    {
        var baseMethodName = lifetime switch
        {
            ServiceLifetime.Singleton => "AddSingleton",
            ServiceLifetime.Transient => "AddTransient",
            ServiceLifetime.Scoped => "AddScoped",
            _ => throw new ArgumentOutOfRangeException(nameof(lifetime), lifetime, "unknown lifetime value"),
        };
        return baseMethodName;
    }

    /// <summary>
    /// Internal exception, thrown to terminate the generation when the InjectAs annotation is not valid.
    /// </summary>
    /// <param name="message">Details about why the annotation was not valid.</param>
    private sealed class InvalidAnnotationException(string message) : ApplicationException(message);
}
