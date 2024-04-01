// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Hosting.Generators;

using System;
using System.Collections.Immutable;
using System.ComponentModel;
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
        isEnabledByDefault: true);

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
                .Where(a => string.Equals(a.AttributeClass?.Name, nameof(InjectAsAttribute), StringComparison.Ordinal));
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

        var contractTypeFullName = targetSymbol.ToDisplayString();

        var contractTypeArg = attribute.NamedArguments
            .FirstOrDefault(a => string.Equals(a.Key, nameof(InjectAsAttribute.ContractType), StringComparison.Ordinal))
            .Value;
        if (contractTypeArg is { IsNull: false, Value: ITypeSymbol contractTypeSymbol })
        {
            contractTypeFullName = contractTypeSymbol.ToDisplayString();
        }

        var implTypeArg = attribute.NamedArguments
            .FirstOrDefault(
                namedArgument => string.Equals(
                    namedArgument.Key,
                    nameof(InjectAsAttribute.ImplementationType),
                    StringComparison.Ordinal))
            .Value;

        var implTypeFullName = GetImplementationTypeFullName(
            spc,
            injectionContext,
            targetSymbol,
            implTypeArg,
            contractTypeFullName);

        var keyParamSegment = GetKeyParameter(attribute);
        if (keyParamSegment.Length != 0)
        {
            methodName = methodName.Replace("Add", "AddKeyed");
        }

        var genericTypeParams = GetGenericTypeParams(targetSymbol, contractTypeFullName, implTypeFullName);

        var methodCallSegment = $"services.{methodName}{genericTypeParams}";

        _ = source.Append("        _ = ")
            .Append(methodCallSegment)
            .Append('(')
            .Append(keyParamSegment)
            .AppendLine(");");
    }

    private static string GetGenericTypeParams(
        ITypeSymbol targetSymbol,
        string contractTypeFullName,
        string? implTypeFullName)
    {
        var isInterface = targetSymbol.TypeKind == TypeKind.Interface;
        var implTypeIsContractType = string.Equals(contractTypeFullName, implTypeFullName, StringComparison.Ordinal);

        return isInterface || !implTypeIsContractType
            ? $"<{contractTypeFullName}, {implTypeFullName}>"
            : $"<{contractTypeFullName}>";
    }

    private static string GetKeyParameter(AttributeData attribute)
    {
        string? key = null;
        var keyArg = attribute.NamedArguments
            .FirstOrDefault(a => string.Equals(a.Key, nameof(InjectAsAttribute.Key), StringComparison.Ordinal))
            .Value;
        if (keyArg is { IsNull: false, Value: not null })
        {
            key = keyArg.Value.ToString();
        }

        return key is null ? string.Empty : $"\"{key}\"";
    }

    private static string? GetImplementationTypeFullName(
        SourceProductionContext spc,
        GeneratorAttributeSyntaxContext injectionContext,
        ITypeSymbol targetSymbol,
        TypedConstant implTypeArg,
        string contractTypeFullName)
    {
        string? implTypeFullName = null;

        if (targetSymbol.TypeKind != TypeKind.Interface)
        {
            if (implTypeArg.IsNull)
            {
                implTypeFullName = targetSymbol.ToDisplayString();
            }
            else if (implTypeArg.Value is ITypeSymbol implTypeSymbol)
            {
                implTypeFullName = implTypeSymbol.ToDisplayString();
            }
        }
        else
        {
            if (implTypeArg.IsNull || implTypeArg.Value is not ITypeSymbol implTypeSymbol)
            {
                throw new InvalidAnnotationException(
                    $"annotation placed on a interface type `{contractTypeFullName}` without specifying an implementation type");
            }

            if (!implTypeSymbol.ImplementsInterface(contractTypeFullName))
            {
                spc.ReportDiagnostic(
                    Diagnostic.Create(
                        AnnotationError,
                        injectionContext.TargetNode.GetLocation(),
                        $"The implementation type `{implTypeSymbol}` does not implement the annotated interface `{contractTypeFullName}`."));
                return implTypeFullName;
            }

            implTypeFullName = implTypeSymbol.ToDisplayString();
        }

        return implTypeFullName;
    }

    /// <summary>Gets the method name for the dependency injection based on the specified service lifetime.</summary>
    /// <param name="lifetime">The service lifetime.</param>
    /// <returns>The method name for the dependency injection.</returns>
    /// <exception cref="InvalidEnumArgumentException">If the <paramref name="lifetime" /> value is not a recognized enum value.</exception>
    private static string GetMethodName(ServiceLifetime lifetime) => lifetime switch
    {
        ServiceLifetime.Singleton => "AddSingleton",
        ServiceLifetime.Transient => "AddTransient",
        ServiceLifetime.Scoped => "AddScoped",
        _ => throw new InvalidEnumArgumentException(nameof(lifetime), (int)lifetime, typeof(ServiceLifetime)),
    };

    /// <summary>
    /// Internal exception, thrown to terminate the generation when the InjectAs annotation is not valid.
    /// </summary>
    private sealed class InvalidAnnotationException : Exception
    {
        public InvalidAnnotationException()
        {
        }

        public InvalidAnnotationException(string message)
            : base(message)
        {
        }

        public InvalidAnnotationException(string message, Exception innerException)
            : base(message, innerException)
        {
        }
    }
}
