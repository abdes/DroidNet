// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using System.Security.Cryptography;
using System.Text;
using AwesomeAssertions;
using Microsoft.CodeAnalysis;
using Microsoft.CodeAnalysis.CSharp;

namespace DroidNet.Resources.Generator.Tests;

[TestClass]
[ExcludeFromCodeCoverage]
public class ResourcesGeneratorCollisionsTests
{
    private const string RuntimeStub = @"
namespace DroidNet.Resources
{
    public interface IResourceMap { }

    public static class ResourceExtensions
    {
        public static string GetLocalized(this string value, IResourceMap? resourceMap = null) => value;
        public static string GetLocalized(this string value, System.Reflection.Assembly assembly, IResourceMap? resourceMap = null) => value;
    }
}
";

    public TestContext TestContext { get; set; }

    /// <summary>
    /// Verifies that the generator skips emitting sources when a type named 'Localized' is
    /// already present in a generated namespace, satisfying the requirement to avoid duplicate helpers
    /// and to report DNRESGEN002 when a non-generated Localized is present.
    /// </summary>
    [TestMethod]
    public void Skips_When_Localized_Exists_In_Generated_Namespace()
    {
        const string assemblyName = "Testing";
        var assemblyHash = ComputeHash(assemblyName);
        var generatedNamespace = $"DroidNet.Resources.Generator.Localized_{assemblyHash}";
        var input = $"namespace {generatedNamespace};\n\ninternal static class Localized {{}}\n";

        var driver = TestHelper.CreateGeneratorDriver(RuntimeStub + input, assemblyName: assemblyName);
        var runResult = driver.GetRunResult();

        var generated = runResult.Results.SelectMany(r => r.GeneratedSources).ToArray();
        _ = generated.Should().BeEmpty();
        _ = runResult.Diagnostics.Select(d => d.Id).Should().Contain("DNRESGEN002");

        static string ComputeHash(string input)
        {
            var h = SHA256.HashData(Encoding.UTF8.GetBytes(input));
            var sb = new StringBuilder(8);
            for (var i = 0; i < 4; i++)
            {
                sb.Append(h[i].ToString("x2", System.Globalization.CultureInfo.InvariantCulture));
            }

            return sb.ToString();
        }
    }

    /// <summary>
    /// Ensures the generator still produces helpers when an existing 'Localized' type is
    /// attributed as generated code, meeting the requirement that generated declarations
    /// do not block helper generation.
    /// </summary>
    [TestMethod]
    public void Generates_When_Existing_Localized_Is_GeneratedCode()
    {
        const string input = """
    namespace Testing;

    [System.CodeDom.Compiler.GeneratedCodeAttribute("Manual","1.0.0")]
    internal static class Localized {}
""";

        var driver = TestHelper.CreateGeneratorDriver(RuntimeStub + input, assemblyName: "Testing");
        var runResult = driver.GetRunResult();

        var generated = runResult.Results.SelectMany(r => r.GeneratedSources).ToArray();
        _ = generated.Length.Should().BePositive();
        _ = generated.Select(gs => gs.SourceText.ToString()).Should().Contain(src => src.Contains("public static string L(this string value", StringComparison.Ordinal));
    }

    /// <summary>
    /// Ensures the generator still emits helper sources even when a referenced (metadata)
    /// assembly defines a 'Localized' type, satisfying the requirement to only consider
    /// compilation-local blocking types and to tolerate metadata-only definitions.
    /// </summary>
    [TestMethod]
    public void Generates_When_Referenced_Assembly_Defines_Localized()
    {
        const string input = """
namespace Testing;

public class Foo {}
""";

        // Create a referenced assembly that defines Testing.Localized (metadata-only)
        const string refSource = """
namespace Testing;

internal static class Localized {}
""";

        var refTree = CSharpSyntaxTree.ParseText(refSource, cancellationToken: this.TestContext.CancellationToken);
        var refCompilation = CSharpCompilation.Create(
            "Testing.Ref",
            [refTree],
            Basic.Reference.Assemblies.Net80.References.All,
            new CSharpCompilationOptions(OutputKind.DynamicallyLinkedLibrary));

        using var ms = new MemoryStream();
        var emitResult = refCompilation.Emit(ms, cancellationToken: this.TestContext.CancellationToken);
        _ = emitResult.Success.Should().BeTrue("Referenced compilation should emit successfully");
        var metadata = ms.ToArray();
        var metadataRef = MetadataReference.CreateFromImage(metadata);

        var driver = TestHelper.CreateGeneratorDriver(
            RuntimeStub + input,
            assemblyName: "Testing",
            additionalReferences: [metadataRef]);
        var runResult = driver.GetRunResult();

        var generated = runResult.Results.SelectMany(r => r.GeneratedSources).ToArray();
        _ = generated.Length.Should().BePositive("Generator should produce sources even when Localized exists in a referenced assembly (metadata)");
    }
}
