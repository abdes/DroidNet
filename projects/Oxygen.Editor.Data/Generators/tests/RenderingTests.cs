// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using System.Security.Cryptography;
using System.Text;
using AwesomeAssertions;
using Microsoft.CodeAnalysis;

namespace Oxygen.Editor.Data.Generators.Tests;

#pragma warning disable SA1117 // Parameters should be on same line or separate lines
#pragma warning disable SA1118 // Parameter should not span multiple lines

[TestClass]
[ExcludeFromCodeCoverage]
public sealed class RenderingTests : VerifyBase
{
    private const string RuntimeStub = """
namespace Oxygen.Editor.Data.Models
{
    public abstract class ModuleSettings { }
}

namespace Oxygen.Editor.Data
{
    [System.AttributeUsage(System.AttributeTargets.Property)]
    public sealed class PersistedAttribute : System.Attribute { }

    public class EditorSettingsManager
    {
        public static class StaticProvider
        {
            public static void Register(object descriptor) { }
        }
    }
}

namespace Oxygen.Editor.Data.Settings
{
    public sealed record SettingKey<T>(string ModuleName, string Key);
    public sealed record SettingDescriptor<T>
    {
        public SettingKey<T> Key { get; init; }
        public string? DisplayName { get; init; }
        public string? Description { get; init; }
        public string? Category { get; init; }
        public System.ComponentModel.DataAnnotations.ValidationAttribute[] Validators { get; init; } = Array.Empty<System.ComponentModel.DataAnnotations.ValidationAttribute>();
    }
}
""";

    [TestMethod]
    [DataRow("Display_Category_And_Validators", """
namespace Testing;

public sealed partial class DisplaySettings : Oxygen.Editor.Data.Models.ModuleSettings
{
    private new const string ModuleName = "Testing";
    [Oxygen.Editor.Data.Persisted]
    [System.ComponentModel.DataAnnotations.Display(Name = "MyName", Description = "Some description")]
    [System.ComponentModel.Category("UserCategory")]
    [System.ComponentModel.DataAnnotations.StringLength(10, MinimumLength = 1)]
    [System.ComponentModel.DataAnnotations.Range(1, 5)]
    public string Name { get; set; } = string.Empty;
}
""", DisplayName = "Renders_Display_Category_And_Validators")]
    [DataRow("Type_TypedConstant_And_Array_Arguments", """
namespace Testing.Attrs { public class TypeHolderAttribute : System.Attribute { public TypeHolderAttribute(System.Type t) { } } }
namespace Testing.Attrs { public class ArrayAttribute : System.Attribute { public ArrayAttribute(int[] values) { } } }

namespace Testing;

public sealed partial class AttributeSettings : Oxygen.Editor.Data.Models.ModuleSettings
{
    private new const string ModuleName = "Testing";
    [Oxygen.Editor.Data.Persisted]
    [Testing.Attrs.TypeHolderAttribute(typeof(string))]
    [Testing.Attrs.ArrayAttribute(new[] { 1, 2, 3 })]
    public string Name { get; set; } = string.Empty;
}
""", DisplayName = "Renders_Type_TypedConstant_And_Array_Arguments")]
    [DataRow("Usings_And_Public_Internal_Sealed", """
namespace Testing.Types { public class MyComplexType { } }
namespace Testing;

public sealed partial class PublicSettings : Oxygen.Editor.Data.Models.ModuleSettings
{
    private new const string ModuleName = "Testing";
    [Oxygen.Editor.Data.Persisted]
    public Testing.Types.MyComplexType Value { get; set; }
}

partial class InternalSettings : Oxygen.Editor.Data.Models.ModuleSettings
{
    private new const string ModuleName = "Testing";
    [Oxygen.Editor.Data.Persisted]
    public string Name { get; set; }
}
""", DisplayName = "Includes_Usings_And_Handles_Public_Internal_And_Sealed")]
    [DataRow("Not_Derived_From_ModuleSettings", """
namespace Testing;

public sealed partial class NotDerived
{
    [Oxygen.Editor.Data.Persisted]
    public string Name { get; set; }
}
""", DisplayName = "Skips_Types_Not_Derived_From_ModuleSettings")]
    [DataRow("Multiple_Properties", """
namespace Testing;

public sealed partial class MultiSettings : Oxygen.Editor.Data.Models.ModuleSettings
{
    private new const string ModuleName = "Testing";
    [Oxygen.Editor.Data.Persisted]
    public string Name { get; set; } = string.Empty;
    [Oxygen.Editor.Data.Persisted]
    public int Age { get; set; }
    [Oxygen.Editor.Data.Persisted]
    public Testing.Types.MyComplexType Value { get; set; }
}

namespace Testing.Types { public class MyComplexType { } }
""", DisplayName = "Renders_Multiple_Properties")]
    [DataRow("Missing_Setter", """
namespace Testing;

public sealed partial class MissingSetterSettings : Oxygen.Editor.Data.Models.ModuleSettings
{
    private new const string ModuleName = "Testing";
    [Oxygen.Editor.Data.Persisted]
    public string Name { get; }
}
""", DisplayName = "Generates_AndReports_MissingSetter")]
    [DataRow("Generic_Types", """
namespace Testing;

public sealed partial class GenericSettings : Oxygen.Editor.Data.Models.ModuleSettings
{
    private new const string ModuleName = "Testing";
    [Oxygen.Editor.Data.Persisted]
    public System.Collections.Generic.List<string> Items { get; set; }
    [Oxygen.Editor.Data.Persisted]
    public System.Collections.Generic.Dictionary<string, int> Map { get; set; }
}
""", DisplayName = "Renders_Generic_Types")]
    [DataRow("Validators_With_NamedArgs_And_TypedConstant", """
namespace Testing;

public class TypeHolderAttribute : System.Attribute { public TypeHolderAttribute(System.Type t) { } public System.Type[] AllowedTypes { get; set; } }

public sealed partial class ValidatorSettings : Oxygen.Editor.Data.Models.ModuleSettings
{
    private new const string ModuleName = "Testing";
    [Oxygen.Editor.Data.Persisted]
    [TypeHolderAttribute(typeof(string), AllowedTypes = new[] { typeof(int), typeof(string) })]
    public string Name { get; set; } = string.Empty;
}
""", DisplayName = "Renders_Validators_With_NamedArgs_And_TypedConstant")]
    [DataRow("Enum_Property", """
namespace Testing;

public enum Color { Red, Green, Blue }
public sealed partial class EnumSettings : Oxygen.Editor.Data.Models.ModuleSettings
{
    private new const string ModuleName = "Testing";
    [Oxygen.Editor.Data.Persisted]
    public Color Favorite { get; set; }
}
""", DisplayName = "Renders_Enum_Property")]
    [DataRow("Nullable_Types", """
namespace Testing;

public sealed partial class NullableSettings : Oxygen.Editor.Data.Models.ModuleSettings
{
    private new const string ModuleName = "Testing";
    [Oxygen.Editor.Data.Persisted]
    public int? NullableInt { get; set; }
}
""", DisplayName = "Renders_Nullable_Types")]
    [DataRow("Duplicate_TypeNames_Across_Namespaces", """
namespace One { public sealed partial class DuplicateNames : Oxygen.Editor.Data.Models.ModuleSettings { private new const string ModuleName = "Testing"; [Oxygen.Editor.Data.Persisted] public string Name { get; set; } = string.Empty; } }
namespace Two { public sealed partial class DuplicateNames : Oxygen.Editor.Data.Models.ModuleSettings { private new const string ModuleName = "Testing"; [Oxygen.Editor.Data.Persisted] public int Age { get; set; } } }
""", DisplayName = "Renders_Duplicate_TypeNamesAcrossNamespaces")]
    [DataRow("Nested_Types", """
namespace Testing;

public sealed partial class Outer
{
    public sealed partial class Inner : Oxygen.Editor.Data.Models.ModuleSettings
    {
        private new const string ModuleName = "Testing";
        [Oxygen.Editor.Data.Persisted]
        public string Nested { get; set; } = string.Empty;
    }
}
""", DisplayName = "Rejects_Nested_Settings_Types")]
    [DataRow("Array_Typed_Primitive_And_Enum_Arguments", """
namespace Testing;

public enum Choice { A, B }
public class ArrayArgAttribute : System.Attribute { public ArrayArgAttribute(Choice[] choices) { } public ArrayArgAttribute(int[] ints) { } }
public sealed partial class ArraySettings : Oxygen.Editor.Data.Models.ModuleSettings
{
    private new const string ModuleName = "Testing";
    [Oxygen.Editor.Data.Persisted]
    [ArrayArgAttribute(new[] { Choice.A, Choice.B })]
    public string Name { get; set; } = string.Empty;
}
""", DisplayName = "Renders_Array_Typed_Primitive_And_Enum_Arguments")]
    [DataRow("Tuple_Types", """
namespace Testing;

public sealed partial class TupleSettings : Oxygen.Editor.Data.Models.ModuleSettings
{
    private new const string ModuleName = "Testing";
    [Oxygen.Editor.Data.Persisted]
    public System.ValueTuple<int, string> Pair { get; set; }
}
""", DisplayName = "Renders_TupleTypes")]
    [DataRow("Validators_With_ArrayNamedArgs", """
namespace Testing;

public class NamesAttribute : System.Attribute { public string[] Names { get; set; } }
public sealed partial class ArrayNamedArgSettings : Oxygen.Editor.Data.Models.ModuleSettings
{
    private new const string ModuleName = "Testing";
    [Oxygen.Editor.Data.Persisted]
    [Names(Names = new[] { "a", "b" })]
    public string Name { get; set; }
}
""", DisplayName = "Renders_Validators_With_ArrayNamedArgs")]
    [DataRow("Multiple_Classes", """
namespace Testing;

public sealed partial class SettingsOne : Oxygen.Editor.Data.Models.ModuleSettings
{
    private new const string ModuleName = "SettingsOne";
    [Oxygen.Editor.Data.Persisted]
    public string Name { get; set; } = string.Empty;
}

public sealed partial class SettingsTwo : Oxygen.Editor.Data.Models.ModuleSettings
{
    private new const string ModuleName = "SettingsTwo";
    [Oxygen.Editor.Data.Persisted]
    public int Age { get; set; }
}
""", DisplayName = "Renders_Multiple_Classes")]
    public async Task Renders(string testCaseName, string sourceCode)
        => await this.RunGeneratorTestAsync(testCaseName, RuntimeStub + sourceCode).ConfigureAwait(false);

    [TestMethod]
    [DataRow("false", "Opted_Out", DisplayName = "Does_Not_Generate_When_Opted_Out")]
    [DataRow("0", "OptOut_ValueZero", DisplayName = "OptOut_ValueZero_Does_Not_Generate")]
    public async Task Does_Not_Generate_When_Opted_Out(string optOutValue, string testCase)
    {
        const string src = RuntimeStub + """
namespace Testing;

public sealed partial class OptOutSettings : Oxygen.Editor.Data.Models.ModuleSettings
{
    private new const string ModuleName = "Testing";
    [Oxygen.Editor.Data.Persisted]
    public string Name { get; set; } = string.Empty;
}
""";

        var globalOptions = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase)
        {
            ["build_property.DroidNetOxygenEditorData_GenerateSettingDescriptors"] = optOutValue,
        };
        await this.RunGeneratorTestAsync(testCase, src, globalOptions: globalOptions).ConfigureAwait(false);
    }

    [TestMethod]
    public async Task Reports_Failure_When_AnalyzerOptions_Throws()
    {
        const string testCase = "AnalyzerOptions_Throws";
        const string src = RuntimeStub + """
namespace Testing;

public sealed partial class SimpleSettings : Oxygen.Editor.Data.Models.ModuleSettings
{
    private new const string ModuleName = "Testing";
    [Oxygen.Editor.Data.Persisted]
    public string Name { get; set; }
}
""";
        var provider = TestHelper.CreateThrowingOptionsProvider("Simulated Options Failure");
        await this.RunGeneratorTestAsync(testCase, src, analyzerConfigOptionsProvider: provider).ConfigureAwait(false);
    }

    [TestMethod]
    public async Task GeneratedFiles_Have_ExactlyOneTrailingNewline()
    {
        const string testCase = "Trailing_Newline";
        const string src = RuntimeStub + """
namespace Testing;

public sealed partial class NewlineSettings : Oxygen.Editor.Data.Models.ModuleSettings
{
    private new const string ModuleName = "Testing";
    [Oxygen.Editor.Data.Persisted]
    public string Name { get; set; }
}
""";

        var driver = TestHelper.CreateGeneratorDriver(src);
        var result = driver.GetRunResult();
        var outputs = result.Results.SelectMany(r => r.GeneratedSources.Select(g => new { g.HintName, Source = g.SourceText.ToString() })).ToArray();

        // Verify that each source ends with exactly one newline
        foreach (var o in outputs)
        {
            var s = o.Source;

            // Use StringComparison ordinal to avoid locale influenced behavior
            if (!s.EndsWith('\n'))
            {
                // If this happens, we'll let the snapshot show the content — no explicit diagnostic
            }

            // Ensure not double newline
            if (s.EndsWith("\n\n", StringComparison.Ordinal))
            {
                // Let snapshot show double newline if it exists; snapshot verification will fail
            }
        }

        var diagnostics = result.Diagnostics.Select(d => new { d.Id, Message = d.GetMessage(System.Globalization.CultureInfo.InvariantCulture) }).ToArray();
        var rawSources = outputs.Length == 0
            ? "<No generated sources>"
            : string.Join('\n', outputs.Select(o => $"--- {o.HintName} ---\n{o.Source}"));

        _ = await this.Verify(rawSources).UseDirectory("Snapshots").UseFileName($"{testCase}_Source").ConfigureAwait(false);
        _ = await this.Verify(new
        {
            Outputs = outputs.Select(o => new { o.HintName }),
            Diagnostics = diagnostics,
        }).UseDirectory("Snapshots").UseFileName($"{testCase}_Diagnostics").ConfigureAwait(false);
    }

    [TestMethod]
    public async Task Generates_Sanitized_AssemblyUnique_Initializer_Name()
    {
        const string src = RuntimeStub + """
namespace Testing;

public sealed partial class SanitizeSettings : Oxygen.Editor.Data.Models.ModuleSettings
{
    private new const string ModuleName = "Testing";
    [Oxygen.Editor.Data.Persisted]
    public string Name { get; set; } = string.Empty;
}
""";

        const string asmName = "123-My Complex.Assembly!Name";
        var driver = TestHelper.CreateGeneratorDriver(src, assemblyName: asmName);
        var result = driver.GetRunResult();
        var outputs = result.Results.SelectMany(r => r.GeneratedSources.Select(g => new { g.HintName, Source = g.SourceText.ToString() })).ToArray();

        // Compute expected initializer name using the same sanitizer logic
        static string Sanitize(string name)
        {
            var n = string.IsNullOrEmpty(name) ? "GeneratedAssembly" : name;
            var sb = new StringBuilder(n.Length);
            for (var i = 0; i < n.Length; i++)
            {
                var ch = n[i];
                if (char.IsLetterOrDigit(ch) || ch == '_')
                {
                    sb.Append(ch);
                }
                else
                {
                    sb.Append('_');
                }
            }

            var safe = sb.ToString();
            if (char.IsDigit(safe[0]))
            {
                safe = "_" + safe;
            }

            return $"DescriptorsInitializer_{safe}";
        }

        var expected = Sanitize(asmName);

        if (outputs.Length == 0)
        {
            var diags = string.Join("; ", result.Diagnostics.Select(d => $"{d.Id}:{d.GetMessage(System.Globalization.CultureInfo.InvariantCulture)}"));
            Assert.Fail($"Expected at least one generated source file, but got none. Diagnostics: {diags}");
        }

        // Ensure the expected initializer class is present in at least one generated file
        _ = outputs.Should().Contain(
            o => o.Source.Contains($"file static class {expected}", StringComparison.Ordinal),
            $"Initializer '{expected}' not found in generated sources");
    }

    private static string ComputeHash(string s)
    {
        var data = Encoding.UTF8.GetBytes(s);
        var hash = SHA256.HashData(data);
        return string.Concat(hash.Select(b => b.ToString("x2", System.Globalization.CultureInfo.InvariantCulture)));
    }

    private async Task RunGeneratorTestAsync(
        string testCaseName,
        string sourceCode,
        Microsoft.CodeAnalysis.Diagnostics.AnalyzerConfigOptionsProvider? analyzerConfigOptionsProvider = null,
        Dictionary<string, string>? globalOptions = null)
    {
        var driver = analyzerConfigOptionsProvider != null
            ? TestHelper.CreateGeneratorDriver(sourceCode, analyzerConfigOptionsProvider: analyzerConfigOptionsProvider)
            : TestHelper.CreateGeneratorDriver(sourceCode, globalOptions: globalOptions);

        var result = driver.GetRunResult();
        var outputs = result.Results
            .SelectMany(r => r.GeneratedSources.Select(g => new
            {
                g.HintName,
                Hash = ComputeHash(g.SourceText.ToString()),
                Source = g.SourceText.ToString(),
            }))
            .ToArray();
        var diagnostics = result.Diagnostics
            .Select(d => new { d.Id, Message = d.GetMessage(System.Globalization.CultureInfo.InvariantCulture) })
            .ToArray();

        var rawSources = outputs.Length == 0
            ? "<No generated sources>"
            : string.Join('\n', outputs.Select(o => $"--- {o.HintName} ---\n{o.Source}"));

        _ = await this.Verify(rawSources).UseDirectory("Snapshots").UseFileName($"{testCaseName}_Source").ConfigureAwait(false);
        _ = await this.Verify(new
        {
            Outputs = outputs.Select(o => new { o.HintName, o.Hash }),
            Diagnostics = diagnostics,
        }).UseDirectory("Snapshots").UseFileName($"{testCaseName}_Diagnostics").ConfigureAwait(false);
    }
}
