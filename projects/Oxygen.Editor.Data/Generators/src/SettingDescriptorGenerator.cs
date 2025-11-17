// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using System.Text;
using Microsoft.CodeAnalysis;
using Microsoft.CodeAnalysis.CSharp;
using Microsoft.CodeAnalysis.CSharp.Syntax;
using Microsoft.CodeAnalysis.Text;

namespace Oxygen.Editor.Data.Generators;

/// <summary>
/// Source generator for setting descriptors used for Oxygen Editor persistence of module settings.
/// </summary>
[System.Diagnostics.CodeAnalysis.SuppressMessage("RoslynDiagnostics", "RS2008", Justification = "This project produces a runtime source generator (not a shipped analyzer)")]
[System.Diagnostics.CodeAnalysis.SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "Report errors as diagnostics instead of crashing the compiler")]
[Generator]
public sealed class SettingDescriptorGenerator : IIncrementalGenerator
{
    private const string TemplateResourceFile = "DescriptorsTemplate.hbs";

    static SettingDescriptorGenerator()
    {
        DependencyLoader.EnsureHandlebarsLoaded();
    }

    /// <summary>
    /// Initializes the generator and registers the incremental pipeline that finds persisted properties
    /// and generates SettingDescriptor sources.
    /// </summary>
    /// <param name="context">The <see cref="IncrementalGeneratorInitializationContext"/> used to register the pipeline.</param>
    public void Initialize(IncrementalGeneratorInitializationContext context)
    {
        var persistedProps = context.SyntaxProvider.ForAttributeWithMetadataName(
            "Oxygen.Editor.Data.PersistedAttribute",
            static (node, _) => node is PropertyDeclarationSyntax,
            static (syntaxContext, _) => (IPropertySymbol)syntaxContext.TargetSymbol);
        var compilationAndProps = context.CompilationProvider.Combine(persistedProps.Collect());
        var compilationAndPropsAndOptions = compilationAndProps.Combine(context.AnalyzerConfigOptionsProvider);
        context.RegisterSourceOutput(compilationAndPropsAndOptions, static (spc, data) =>
        {
            try
            {
                var ((compilation, properties), optionsProvider) = data;

                // Respect an explicit opt-out first; users can set the property in their project file.
                const string BuildPropertyName = "build_property.DroidNetOxygenEditorData_GenerateSettingDescriptors";
                if (optionsProvider.GlobalOptions.TryGetValue(BuildPropertyName, out var optOut))
                {
                    if (string.Equals(optOut, "false", StringComparison.OrdinalIgnoreCase) || string.Equals(optOut, "0", StringComparison.OrdinalIgnoreCase))
                    {
                        return; // opted out
                    }
                }

                var asmName = compilation.AssemblyName ?? "GeneratedAssembly";

                // Report that the generator has started to help debugging
                spc.ReportDiagnostic(Diagnostic.Create(Diagnostics.GeneratorStarted, Location.None, asmName));
                GenerateSources(spc, compilation, asmName, properties);
            }
            catch (Exception ex)
            {
                // Only include the exception message in the diagnostic to avoid stack traces in test output.
                Diagnostics.Report(spc, Diagnostics.GeneratorFailure, Location.None, ex.Message);
            }
        });
    }

    private static void GenerateSources(SourceProductionContext spc, Compilation compilation, string asmName, ImmutableArray<IPropertySymbol> persistedProperties)
    {
        var persistedByType = BuildPersistedByTypeDictionary(persistedProperties);

        if (persistedByType.Count == 0)
        {
            return; // nothing to do
        }

        // We'll only process types that derive from ModuleSettings
        var moduleSettingsSymbol = compilation.GetTypeByMetadataName("Oxygen.Editor.Data.Models.ModuleSettings");
        if (moduleSettingsSymbol == null)
        {
            return; // missing runtime type - cannot proceed
        }

        var usings = new HashSet<string>(StringComparer.Ordinal)
        {
            "System.ComponentModel.DataAnnotations",
            "Oxygen.Editor.Data.Settings",
            "Oxygen.Editor.Data",
            "System.Runtime.CompilerServices",
        };

        var namespaces = new Dictionary<string, NamespaceModel>(StringComparer.Ordinal);

        var namespaceDict = BuildNamespaceModels(persistedByType, moduleSettingsSymbol, compilation, usings, spc);
        foreach (var kv in namespaceDict)
        {
            if (kv.Value.Types.Count > 0)
            {
                namespaces.Add(kv.Key, kv.Value);
            }
        }

        // If no types built, exit
        if (namespaces.Count == 0)
        {
            return;
        }

        var model = new GeneratorModel
        {
            AssemblyName = asmName,
            Usings = [.. usings.OrderBy(x => x, StringComparer.Ordinal)],
            Namespaces = [.. namespaces.Values.OrderBy(n => n.Name, StringComparer.Ordinal)],
        };

        RenderAndAddSource(spc, model, asmName);
    }

    private static Dictionary<INamedTypeSymbol, IPropertySymbol[]> BuildPersistedByTypeDictionary(ImmutableArray<IPropertySymbol> persistedProperties)
    {
        var map = new Dictionary<INamedTypeSymbol, List<IPropertySymbol>>(SymbolEqualityComparer.Default);
        foreach (var p in persistedProperties)
        {
            if (p.ContainingType is not INamedTypeSymbol nt)
            {
                continue;
            }

            if (!map.TryGetValue(nt, out var list))
            {
                list = [];
                map[nt] = list;
            }

            list.Add(p);
        }

        var persistedByType = new Dictionary<INamedTypeSymbol, IPropertySymbol[]>(SymbolEqualityComparer.Default);
        foreach (var kv in map)
        {
            persistedByType[kv.Key] = [.. kv.Value];
        }

        return persistedByType;
    }

    private static TypeModel? BuildTypeModel(INamedTypeSymbol containingType, SourceProductionContext spc)
    {
        // Validate the class: must be partial
        var syntaxRef = containingType.DeclaringSyntaxReferences.FirstOrDefault();
        if (syntaxRef?.GetSyntax(spc.CancellationToken) is ClassDeclarationSyntax cd && !cd.Modifiers.Any(m => m.IsKind(SyntaxKind.PartialKeyword)))
        {
            Diagnostics.Report(spc, Diagnostics.ClassMustBePartial, containingType.Locations.FirstOrDefault(), containingType.Name);
            return null;
        }

        // Must have private new const string ModuleName field
        var moduleNameField = containingType.GetMembers().OfType<IFieldSymbol>().FirstOrDefault(f => string.Equals(f.Name, "ModuleName", StringComparison.Ordinal));
        if (moduleNameField == null)
        {
            Diagnostics.Report(spc, Diagnostics.ModuleNameMissing, containingType.Locations.FirstOrDefault(), containingType.Name);
            return null;
        }

        if (!moduleNameField.IsConst || moduleNameField.Type?.SpecialType != SpecialType.System_String)
        {
            Diagnostics.Report(spc, Diagnostics.ModuleNameNotConst, moduleNameField.Locations.FirstOrDefault(), containingType.Name);
            return null;
        }

        if (moduleNameField.ConstantValue is not string moduleNameValue)
        {
            Diagnostics.Report(spc, Diagnostics.ModuleNameNotConst, moduleNameField.Locations.FirstOrDefault(), containingType.Name);
            return null;
        }

        var typeModel = new TypeModel
        {
            Name = containingType.Name,
            IsPublic = containingType.DeclaredAccessibility == Accessibility.Public,
            IsSealed = containingType.IsSealed,
            ModuleName = moduleNameValue,
            FullName = BuildFullTypeName(containingType),
        };

        return typeModel;
    }

    private static PropertyModel? BuildPropertyModel(IPropertySymbol prop, Compilation compilation, HashSet<string> usings, SourceProductionContext spc, INamedTypeSymbol containingType)
    {
        // Property validation
        if (prop.DeclaredAccessibility != Accessibility.Public)
        {
            Diagnostics.Report(spc, Diagnostics.PersistedPropertyNotPublic, prop.Locations.FirstOrDefault(), prop.Name, containingType.Name);
        }

        if (prop.SetMethod == null)
        {
            Diagnostics.Report(spc, Diagnostics.PersistedPropertyMissingSetter, prop.Locations.FirstOrDefault(), prop.Name, containingType.Name);
        }

        if (!IsLikelySerializable(prop.Type))
        {
            Diagnostics.Report(spc, Diagnostics.PersistedPropertyNotSerializable, prop.Locations.FirstOrDefault(), prop.Name, containingType.Name);
            return null;
        }

        var propModel = new PropertyModel
        {
            Name = prop.Name,
            Type = prop.Type.ToDisplayString(SymbolDisplayFormat.MinimallyQualifiedFormat),
        };

        // Display attribute
        var displayAttr = prop.GetAttributes().FirstOrDefault(a => string.Equals(a.AttributeClass?.ToDisplayString(), "System.ComponentModel.DataAnnotations.DisplayAttribute", StringComparison.Ordinal));
        var displayName = displayAttr?.NamedArguments.FirstOrDefault(kvp => string.Equals(kvp.Key, "Name", StringComparison.Ordinal)).Value.Value as string;
        var description = displayAttr?.NamedArguments.FirstOrDefault(kvp => string.Equals(kvp.Key, "Description", StringComparison.Ordinal)).Value.Value as string;
        propModel.DisplayNameLiteral = LiteralOrNull(displayName);
        propModel.DescriptionLiteral = LiteralOrNull(description);

        // Category
        var catAttr = prop.GetAttributes().FirstOrDefault(a => string.Equals(a.AttributeClass?.ToDisplayString(), "System.ComponentModel.CategoryAttribute", StringComparison.Ordinal));
        var category = catAttr?.ConstructorArguments.FirstOrDefault().Value as string;
        propModel.CategoryLiteral = LiteralOrNull(category);

        // validators
        var validators = GetValidators(prop, compilation);
        propModel.Validators.AddRange(validators);

        // add using for the property type and validators
        AddUsingsForProperty(prop, usings);

        return propModel;
    }

    private static List<ValidatorModel> GetValidators(IPropertySymbol prop, Compilation compilation)
    {
        var result = new List<ValidatorModel>();
        var validatorTypeSymbol = compilation.GetTypeByMetadataName("System.ComponentModel.DataAnnotations.ValidationAttribute");
        var validators = prop.GetAttributes().Where(a => a.AttributeClass != null && IsAssignableTo(a.AttributeClass, validatorTypeSymbol)).ToList();
        foreach (var v in validators)
        {
            result.Add(new ValidatorModel { Rendered = RenderAttribute(v) });
        }

        return result;
    }

    private static void AddUsingsForProperty(IPropertySymbol prop, HashSet<string> usings)
    {
        var typeNs = prop.Type.ContainingNamespace?.ToDisplayString();
        if (!string.IsNullOrEmpty(typeNs))
        {
            usings.Add(typeNs!);
        }

        foreach (var a in prop.GetAttributes())
        {
            var aNs = a.AttributeClass?.ContainingNamespace?.ToDisplayString();
            if (!string.IsNullOrEmpty(aNs))
            {
                usings.Add(aNs!);
            }
        }
    }

    private static Dictionary<string, NamespaceModel> BuildNamespaceModels(Dictionary<INamedTypeSymbol, IPropertySymbol[]> persistedByType, INamedTypeSymbol moduleSettingsSymbol, Compilation compilation, HashSet<string> usings, SourceProductionContext spc)
    {
        var namespaces = new Dictionary<string, NamespaceModel>(StringComparer.Ordinal);

        foreach (var kv in persistedByType)
        {
            if (spc.CancellationToken.IsCancellationRequested)
            {
                return namespaces;
            }

            var containingType = kv.Key;
            if (!IsDerivedFrom(containingType, moduleSettingsSymbol))
            {
                continue; // only emit for types that derive from ModuleSettings
            }

            var nsName = containingType.ContainingNamespace?.ToDisplayString() ?? string.Empty;

            // Skip nested types: only top-level ModuleSettings are supported. If a nested type has persisted properties, report an error diagnostic.
            if (containingType.ContainingType != null)
            {
                foreach (var p in kv.Value)
                {
                    Diagnostics.Report(spc, Diagnostics.PersistedPropertyNested, p.Locations.FirstOrDefault(), p.Name, containingType.Name);
                }

                continue;
            }

            if (!namespaces.TryGetValue(nsName, out var nsModel))
            {
                nsModel = new NamespaceModel { Name = nsName };
                namespaces.Add(nsName, nsModel);
            }

            var typeModel = BuildTypeModel(containingType, spc);
            if (typeModel == null)
            {
                continue;
            }

            foreach (var prop in kv.Value)
            {
                var propModel = BuildPropertyModel(prop, compilation, usings, spc, containingType);
                if (propModel != null)
                {
                    typeModel.Properties.Add(propModel);
                }
            }

            if (typeModel.Properties.Count == 0)
            {
                continue;
            }

            nsModel.Types.Add(typeModel);
        }

        return namespaces;
    }

    private static string BuildFullTypeName(INamedTypeSymbol containingType)
    {
        // For now the generated descriptor types are emitted as top-level partial classes named by the containing type's simple name.
        // Use the simple type name so the generated registration matches the generated type name.
        return containingType.Name;
    }

    private static void RenderAndAddSource(SourceProductionContext spc, GeneratorModel model, string asmName)
    {
        var template = GetEmbeddedResource(TemplateResourceFile);
        var compiled = HandlebarsDotNet.Handlebars.Compile(template);
        var output = compiled(model);

        // Ensure a single trailing newline only to avoid double blank lines at the end of the generated file.
        // Keep content otherwise unchanged (no normalization) — just trim excessive trailing newlines.
        output = output.TrimEnd('\r', '\n') + "\n";
        var hint = BuildHintFileName("Descriptors.", asmName);
        spc.AddSource(hint, SourceText.From(output, Encoding.UTF8));
    }

    private static string BuildHintFileName(string prefix, string asmName)
    {
        var safeAsm = string.IsNullOrEmpty(asmName) ? "GeneratedAssembly" : asmName;
        return $"{prefix}{safeAsm}.g.cs";
    }

    private static string GetEmbeddedResource(string path)
    {
        var assembly = System.Reflection.Assembly.GetExecutingAssembly();
        var resourceNames = assembly.GetManifestResourceNames();
        var dotPath = path.Replace('/', '.').Replace('\\', '.');
        var match = resourceNames.FirstOrDefault(r => r.Contains(dotPath, StringComparison.OrdinalIgnoreCase))
            ?? throw new InvalidOperationException($"Missing resource '{path}' in the generator package.");

        using var stream = assembly.GetManifestResourceStream(match)!
            ?? throw new InvalidOperationException($"Failed to get manifest resource stream for '{match}'");
        using var reader = new System.IO.StreamReader(stream);
        var content = reader.ReadToEnd();
        return content;
    }

    // Template model classes are defined at the end of the type to follow helper methods;
    private static bool IsDerivedFrom(INamedTypeSymbol type, INamedTypeSymbol? baseType)
    {
        if (baseType == null)
        {
            return false;
        }

        var bt = type.BaseType;
        while (bt != null)
        {
            if (SymbolEqualityComparer.Default.Equals(bt, baseType))
            {
                return true;
            }

            bt = bt.BaseType;
        }

        return false;
    }

    private static bool IsAssignableTo(INamedTypeSymbol symbol, INamedTypeSymbol? baseType)
    {
        if (baseType == null)
        {
            return false;
        }

        var current = symbol;
        while (current != null)
        {
            if (SymbolEqualityComparer.Default.Equals(current, baseType))
            {
                return true;
            }

            current = current.BaseType;
        }

        return false;
    }

    private static bool IsLikelySerializable(ITypeSymbol type)
    {
        if (type == null)
        {
            return false;
        }

        switch (type.TypeKind)
        {
            case TypeKind.Delegate:
            case TypeKind.FunctionPointer:
            case TypeKind.Pointer:
            case TypeKind.Error:
                return false;
            case TypeKind.Array:
                var arr = (IArrayTypeSymbol)type;
                return IsLikelySerializable(arr.ElementType);
            default:
                return true;
        }
    }

    private static string RenderAttribute(AttributeData attr)
    {
        if (attr.AttributeClass == null)
        {
            return "new ValidationAttribute()";
        }

        var typeName = attr.AttributeClass.Name; // keep 'Attribute' suffix for clarity
        var ctorArgs = attr.ConstructorArguments.Select(FormatTypedConstant);
        var namedArgs = attr.NamedArguments.Select(kvp => $"{kvp.Key} = {FormatTypedConstant(kvp.Value)}");
        var result = new StringBuilder();
        result.Append("new ").Append(typeName).Append('(').Append(string.Join(", ", ctorArgs)).Append(')');
        if (namedArgs.Any())
        {
            result.Append(" { ").Append(string.Join(", ", namedArgs)).Append(" }");
        }

        return result.ToString();
    }

    private static string FormatTypedConstant(TypedConstant tc)
        => tc.IsNull
            ? "null"
            : tc.Kind switch
            {
                TypedConstantKind.Primitive => FormatPrimitive(tc.Value),
                TypedConstantKind.Enum => tc.Type != null ? $"({tc.Type.ToDisplayString(SymbolDisplayFormat.MinimallyQualifiedFormat)}){FormatPrimitive(tc.Value)}" : FormatPrimitive(tc.Value),
                TypedConstantKind.Type => $"typeof({(tc.Value as ITypeSymbol)?.ToDisplayString(SymbolDisplayFormat.MinimallyQualifiedFormat)})",
                TypedConstantKind.Array => "new[] { " + string.Join(", ", tc.Values.Select(FormatTypedConstant)) + " }",
                _ => FormatPrimitive(tc.Value),
            };

    private static string FormatPrimitive(object? value)
        => value switch
        {
            null => "null",
            string s => "@\"" + s.Replace("\"", "\"\"") + "\"",
            char c => "'" + c.ToString().Replace("'", "\\'") + "'",
            bool b => b ? "true" : "false",
            byte or sbyte or short or ushort or int or uint or long or ulong or float or double or decimal => Convert.ToString(value, System.Globalization.CultureInfo.InvariantCulture),
            _ => value.ToString() ?? "null",
        };

    private static string LiteralOrNull(string? s) => s is null ? "null" : "@\"" + s.Replace("\"", "\"\"") + "\"";

    // Template model classes
    private sealed class GeneratorModel
    {
        public string AssemblyName { get; set; } = string.Empty;

        public List<string> Usings { get; set; } = [];

        public List<NamespaceModel> Namespaces { get; set; } = [];
    }

    private sealed class NamespaceModel
    {
        public string Name { get; set; } = string.Empty;

        public List<TypeModel> Types { get; set; } = [];
    }

    private sealed class TypeModel
    {
        public string Name { get; set; } = string.Empty;

        public bool IsPublic { get; set; }

        public bool IsSealed { get; set; }

        public string ModuleName { get; set; } = string.Empty;

        // Full name includes the nested outer type names (e.g. "Outer.Inner").
        // Used for fully-qualified references outside the namespace.
        public string FullName { get; set; } = string.Empty;

        public List<PropertyModel> Properties { get; set; } = [];
    }

    private sealed class PropertyModel
    {
        public string Name { get; set; } = string.Empty;

        public string Type { get; set; } = string.Empty;

        public string DisplayNameLiteral { get; set; } = "null";

        public string DescriptionLiteral { get; set; } = "null";

        public string CategoryLiteral { get; set; } = "null";

        public List<ValidatorModel> Validators { get; set; } = [];
    }

    private sealed class ValidatorModel
    {
        public string Rendered { get; set; } = string.Empty;
    }
}
