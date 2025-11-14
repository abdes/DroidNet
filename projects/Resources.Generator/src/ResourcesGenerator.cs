// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Security.Cryptography;
using System.Text;
using Microsoft.CodeAnalysis;
using Microsoft.CodeAnalysis.Text;

namespace DroidNet.Resources.Generator;

/// <summary>
///     Source generator for DroidNet.Resources. Generates a helper class for resource localization.
/// </summary>
[Generator]
[System.Diagnostics.CodeAnalysis.SuppressMessage("RoslynDiagnostics", "RS2008", Justification = "This project produces a runtime source generator (not a shipped analyzer). Release-tracking is only required for published analyzer packages; suppress RS2008 to avoid noisy warnings in generator-only projects.")]
public class ResourcesGenerator : IIncrementalGenerator
{
    // Diagnostics are centralized in `Diagnostics.cs`.

    /// <inheritdoc/>
    [System.Diagnostics.CodeAnalysis.SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "Report provider exceptions as diagnostics instead of crashing the compiler")]
    public void Initialize(IncrementalGeneratorInitializationContext context)
    {
        // Create smaller incremental providers so the generator work runs only when relevant inputs change.
        var compilationProvider = context.CompilationProvider;
        var optionsProvider = context.AnalyzerConfigOptionsProvider;

        // Detect whether the compilation contains the runtime ResourceExtensions type.
        var resourceExtensionsProvider = compilationProvider.Select((c, ct) => c.GetTypeByMetadataName("DroidNet.Resources.ResourceExtensions"));

        // Combine the detected resource type with the compilation and analyzer options so we can run generation
        // only when the resource type exists and when relevant compilation/options change.
        var trigger = resourceExtensionsProvider.Combine(compilationProvider).Combine(optionsProvider);

        context.RegisterSourceOutput(trigger, (spc, data) =>
        {
            try
            {
                spc.CancellationToken.ThrowIfCancellationRequested();

                var ((resourceExt, compilation), options) = data;

                // Respect an explicit global opt-out first (don't emit diagnostics when opted-out)
                if (options.GlobalOptions.TryGetValue("build_property.DroidNetResources_GenerateHelper", out var optOut))
                {
                    if (string.Equals(optOut, "false", StringComparison.OrdinalIgnoreCase) || string.Equals(optOut, "0", StringComparison.OrdinalIgnoreCase))
                    {
                        return; // user opted out; do not run or emit diagnostics
                    }
                }

                // Report that generator started
                var asmName = compilation.AssemblyName ?? "UnknownAssembly";
                spc.ReportDiagnostic(Diagnostic.Create(Diagnostics.GeneratorStarted, Location.None, asmName));

                // If the runtime type isn't present, nothing to do.
                // Report whether the runtime ResourceExtensions type is present.
                var resourceName = resourceExt?.ToDisplayString() ?? "(none)";
                var foundText = resourceExt != null ? "found" : "not found";
                Diagnostics.Report(spc, Diagnostics.FoundResourceExtensions, Location.None, resourceName, foundText);
                if (resourceExt == null)
                {
                    return;
                }

                Execute(spc, compilation);
            }
            catch (Exception ex)
            {
                Diagnostics.Report(spc, Diagnostics.GeneratorFailure, Location.None, ex.Message);
            }
        });
    }

    [System.Diagnostics.CodeAnalysis.SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "issue diagnostics instead of throwing")]
    private static void Execute(SourceProductionContext spc, Compilation compilation)
    {
        try
        {
            // Determine the project's root namespace via AnalyzerConfigOptions first, fallback to assembly name
            var asmName = compilation.AssemblyName ?? "GeneratedAssembly";

            // Build assembly-based values
            var localizedNamespace = BuildLocalizedNamespace(asmName);

            // Avoid generating duplicate helper if a conflicting Localized type exists.
            var generatedTypeFqn = $"{localizedNamespace}.Localized";
            var (candidate, isUserDefined, candidateLocation) = FindExistingType(compilation, generatedTypeFqn);
            if (candidate != null)
            {
                Diagnostics.Report(spc, Diagnostics.FoundExistingType, candidateLocation, candidate.ToDisplayString(), isUserDefined, candidate.DeclaringSyntaxReferences.Length);

                if (isUserDefined)
                {
                    // User has their own type with this name - skip generation
                    Diagnostics.Report(spc, Diagnostics.SkippingUserDefined, candidateLocation, candidate.ToDisplayString());
                    return;
                }

                // Otherwise, let it regenerate (clean build scenario)
                Diagnostics.Report(spc, Diagnostics.Regenerating, candidateLocation, candidate.ToDisplayString());
            }
            else
            {
                Diagnostics.Report(spc, Diagnostics.NoExisting, Location.None, generatedTypeFqn);
            }

            // Generate the sources (split out for brevity & testability)
            GenerateSources(spc, compilation, localizedNamespace, asmName);

            // Report completion
            var fileCount = compilation.Options.OutputKind is OutputKind.ConsoleApplication or OutputKind.WindowsApplication or OutputKind.WindowsRuntimeApplication ? "2" : "1";
            spc.ReportDiagnostic(Diagnostic.Create(Diagnostics.GenerationComplete, Location.None, asmName, fileCount));
        }
        catch (OperationCanceledException)
        {
            // Cancellation: swallow and let roslyn handle it gracefully
        }
        catch (Exception ex)
        {
            Diagnostics.Report(spc, Diagnostics.GeneratorFailure, Location.None, ex.Message);
        }
    }

    private static string BuildLocalizedNamespace(string asmName)
    {
        var assemblyHash = GetShortHash(asmName);
        return $"DroidNet.Resources.Generator.Localized_{assemblyHash}";
    }

    private static string BuildHintFileName(string prefix, string asmName)
    {
        var safeAsm = string.IsNullOrEmpty(asmName) ? "GeneratedAssembly" : asmName;
        return $"{prefix}{safeAsm}.g.cs";
    }

    // Diagnostic emission now uses the centralized `Diagnostics.Report` helper.
    private static bool IsUserDefinedNonGenerated(INamedTypeSymbol symbol)
    {
        if (symbol == null)
        {
            return false;
        }

        // If not declared in source, it's not a user source declaration in this compilation
        if (symbol.DeclaringSyntaxReferences.Length == 0)
        {
            return false;
        }

        foreach (var attr in symbol.GetAttributes())
        {
            var attrClass = attr.AttributeClass;
            if (attrClass == null)
            {
                continue;
            }

            // Be tolerant of how the attribute is represented in the compilation: check the simple name
            // and the full metadata name.
            if (string.Equals(attrClass.Name, "GeneratedCodeAttribute", StringComparison.Ordinal)
                || string.Equals(attrClass.ToDisplayString(), "System.CodeDom.Compiler.GeneratedCodeAttribute", StringComparison.Ordinal))
            {
                return false; // generated
            }
        }

        return true; // user-defined in source and not marked generated
    }

    private static void GenerateSources(SourceProductionContext spc, Compilation compilation, string localizedNamespace, string asmName)
    {
        var localizedSource = GenerateLocalizedHelperSource(localizedNamespace);
        var localizedHintName = BuildHintFileName("Localization_", asmName);
        spc.AddSource(localizedHintName, SourceText.From(localizedSource, Encoding.UTF8));

        var isExecutable = compilation.Options.OutputKind
            is OutputKind.ConsoleApplication
            or OutputKind.WindowsApplication
            or OutputKind.WindowsRuntimeApplication;

        if (isExecutable)
        {
            // Only generate DI extension when DI framework (DryIoc) is referenced in the compilation.
            var diSymbol = compilation.GetTypeByMetadataName("DryIoc.IContainer");
            if (diSymbol != null)
            {
                var diExtensionSource = GenerateDIExtensionSource();
                var diExtensionHintName = BuildHintFileName("ResourceExtensions_", asmName);
                spc.AddSource(diExtensionHintName, SourceText.From(diExtensionSource, Encoding.UTF8));
            }
            else
            {
                // Report diagnostic: DI framework missing; DI extension not emitted
                Diagnostics.Report(spc, Diagnostics.DIFrameworkMissing, Location.None);
            }
        }
    }

    private static (INamedTypeSymbol? candidate, bool isUserDefined, Location candidateLocation) FindExistingType(Compilation compilation, string generatedTypeFqn)
    {
        var existingGeneratedType = compilation.GetTypeByMetadataName(generatedTypeFqn);

        if (existingGeneratedType != null)
        {
            var candidate = existingGeneratedType!;
            var isUserDefined = IsUserDefinedNonGenerated(candidate);
            var candidateLocation = candidate.Locations.FirstOrDefault() ?? Location.None;
            return (candidate, isUserDefined, candidateLocation);
        }

        return (null, false, Location.None);
    }

    private static string GetShortHash(string input)
    {
        if (string.IsNullOrEmpty(input))
        {
            return "00000000";
        }

        using var sha = SHA256.Create();
        var bytes = Encoding.UTF8.GetBytes(input);
        var hash = sha.ComputeHash(bytes);

        // Return the first 8 hex chars (4 bytes)
        var sb = new StringBuilder(8);
        for (var i = 0; i < 4; i++)
        {
            sb.Append(hash[i].ToString("x2", System.Globalization.CultureInfo.InvariantCulture));
        }

        return sb.ToString();
    }

    private static string GenerateLocalizedHelperSource(string safeNamespace)
        => $$"""
// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

#nullable enable
// <auto-generated />

using System.Reflection;
using System.Runtime.CompilerServices;
using DroidNet.Resources;

namespace {{safeNamespace}};

[System.CodeDom.Compiler.GeneratedCode("DroidNet.Resources.Generator", "1.0.0")]
internal static class ResourcesBootstrap
{
    [ModuleInitializer]
    internal static void InitializeLocalization()
    {
        ResourceExtensions.Initialize(new DefaultResourceMapProvider());
    }
}

[System.CodeDom.Compiler.GeneratedCode("DroidNet.Resources.Generator", "1.0.0")]
internal class AssemblyMarker { }

[System.CodeDom.Compiler.GeneratedCode("DroidNet.Resources.Generator", "1.0.0")]
internal static class Localized
{
    private static readonly Assembly Assembly = typeof(Localized).Assembly;
    public static string L(this string value)
        => ResourceExtensions.L<AssemblyMarker>(value);

    public static string R(this string path)
        => ResourceExtensions.R<AssemblyMarker>(path);
}
""";

    private static string GenerateDIExtensionSource()
        => """
// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

#nullable enable
// <auto-generated />

using DroidNet.Resources;
using DryIoc;

namespace DroidNet.Resources;

[System.CodeDom.Compiler.GeneratedCode("DroidNet.Resources.Generator", "1.0.0")]
public static class LocalizationContainerExtensions
{
    /// <summary>
    ///     Configures localization with the default resource map provider.
    /// </summary>
    /// <param name="container">The DryIoc container.</param>
    /// <returns>The container for method chaining.</returns>
    public static IContainer WithLocalization(this IContainer container)
    {
        // Only register default if nothing is registered yet
        if (!container.IsRegistered<IResourceMapProvider>())
        {
            container.Register<IResourceMapProvider, DefaultResourceMapProvider>(Reuse.Singleton);
        }

        var provider = container.Resolve<IResourceMapProvider>();
        ResourceExtensions.Initialize(provider);
        return container;
    }
}
""";
}
