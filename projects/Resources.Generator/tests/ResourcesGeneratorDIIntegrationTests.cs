// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using AwesomeAssertions;
using Microsoft.CodeAnalysis;

namespace DroidNet.Resources.Generator.Tests;

[TestClass]
[ExcludeFromCodeCoverage]
public class ResourcesGeneratorDIIntegrationTests
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

    /// <summary>
    /// Verifies that when the compilation target is a library, the generator does not
    /// produce the DI extension, satisfying the requirement to avoid generating DI helpers for libraries.
    /// </summary>
    [TestMethod]
    public void Does_Not_Generate_DI_Extension_For_Library()
    {
        const string input = @"
namespace Testing;

public class Foo {}
";

        var driver = TestHelper.CreateGeneratorDriver(RuntimeStub + input, assemblyName: "Testing", outputKind: OutputKind.DynamicallyLinkedLibrary);
        var runResult = driver.GetRunResult();

        var generated = runResult.Results.SelectMany(r => r.GeneratedSources).ToArray();

        // Expect only the Localized helper, not the DI extension
        _ = generated.Select(g => g.HintName).Should().NotContain(h => h.Contains("ResourceExtensions_", StringComparison.Ordinal));

        // Localized helper must still be generated
        const string asmName = "Testing";
        _ = generated.Select(g => g.HintName).Should().Contain(h => h.Contains($"Localization_{asmName}.g.cs", StringComparison.Ordinal));
    }

    /// <summary>
    /// Asserts that the DI extension generated for executables contains expected API members
    /// such as WithLocalization and DefaultResourceMapProvider, fulfilling the requirement
    /// for a usable DI integration surface.
    /// </summary>
    [TestMethod]
    public void DI_Extension_Source_Contains_Expected_Members_For_Executable()
    {
        const string input = @"
namespace Testing;

public class Foo {}
";

        // Create a minimal DryIoc metadata reference so the generator will emit DI extension source
        const string dryIocSource = """
namespace DryIoc {
    public interface IContainer { }
    public static class Reuse { public static object Singleton => null; }
}
""";
        var dryTree = Microsoft.CodeAnalysis.CSharp.CSharpSyntaxTree.ParseText(dryIocSource);
        var dryCompilation = Microsoft.CodeAnalysis.CSharp.CSharpCompilation.Create(
            "DryIoc.Ref",
            new[] { dryTree },
            Basic.Reference.Assemblies.Net80.References.All,
            new Microsoft.CodeAnalysis.CSharp.CSharpCompilationOptions(OutputKind.DynamicallyLinkedLibrary));

        using var ms = new MemoryStream();
        var emitResult = dryCompilation.Emit(ms);
        _ = emitResult.Success.Should().BeTrue("DryIoc reference should compile");
        var metadata = ms.ToArray();
        var metadataRef = MetadataReference.CreateFromImage(metadata);

        var driver = TestHelper.CreateGeneratorDriver(RuntimeStub + input, assemblyName: "Testing", outputKind: OutputKind.ConsoleApplication, additionalReferences: new[] { metadataRef });
        var runResult = driver.GetRunResult();

        var generated = runResult.Results.SelectMany(r => r.GeneratedSources).ToArray();
        var di = generated.SingleOrDefault(g => g.HintName.Contains("ResourceExtensions_", StringComparison.Ordinal));
        _ = di.Should().NotBeNull();

        var src = di!.SourceText.ToString();
        _ = src.Contains("WithLocalization", StringComparison.Ordinal).Should().BeTrue();
        _ = src.Contains("DefaultResourceMapProvider", StringComparison.Ordinal).Should().BeTrue();
        _ = src.Contains("IContainer WithLocalization", StringComparison.Ordinal).Should().BeTrue();
    }

    /// <summary>
    /// Validates that the DI extension is generated for all executable output kinds (Console,
    /// Windows, WindowsRuntime), meeting the requirement to support DI for any executable target.
    /// </summary>
    /// <param name="kind">The output kind being tested.</param>
    [TestMethod]
    [DataRow(OutputKind.ConsoleApplication)]
    [DataRow(OutputKind.WindowsApplication)]
    [DataRow(OutputKind.WindowsRuntimeApplication)]
    public void DI_Extension_Generated_For_All_Executable_Kinds(OutputKind kind)
    {
        const string input = @"
namespace Testing;

public class Foo {}
";

        // Provide a DryIoc metadata reference for DI extension generation
        const string dryIocSource2 = """
    namespace DryIoc { public interface IContainer { } public static class Reuse { public static object Singleton => null; } }
    """;
        var dryTree2 = Microsoft.CodeAnalysis.CSharp.CSharpSyntaxTree.ParseText(dryIocSource2);
        var dryCompilation2 = Microsoft.CodeAnalysis.CSharp.CSharpCompilation.Create(
            "DryIoc.Ref",
            new[] { dryTree2 },
            Basic.Reference.Assemblies.Net80.References.All,
            new Microsoft.CodeAnalysis.CSharp.CSharpCompilationOptions(OutputKind.DynamicallyLinkedLibrary));
        using var ms2 = new MemoryStream();
        var emitResult2 = dryCompilation2.Emit(ms2);
        _ = emitResult2.Success.Should().BeTrue();
        var metadata2 = ms2.ToArray();
        var metadataRef2 = MetadataReference.CreateFromImage(metadata2);

        var driver = TestHelper.CreateGeneratorDriver(RuntimeStub + input, assemblyName: "Testing", outputKind: kind, additionalReferences: new[] { metadataRef2 });
        var runResult = driver.GetRunResult();

        var generated = runResult.Results.SelectMany(r => r.GeneratedSources).ToArray();
        _ = generated.Select(g => g.HintName).Should().Contain(h => h.Contains("ResourceExtensions_", StringComparison.Ordinal));

        // Validate Localized helper filename format
        const string asmName = "Testing";
        _ = generated.Select(g => g.HintName).Should().Contain(h => h.Contains($"Localization_{asmName}.g.cs", StringComparison.Ordinal));
    }

    [TestMethod]
    public void Does_Not_Generate_DI_Extension_When_DryIoc_Not_Referenced()
    {
        const string input = @"
namespace Testing;

public class Foo {}
";

        var driver = TestHelper.CreateGeneratorDriver(RuntimeStub + input, assemblyName: "Testing", outputKind: OutputKind.ConsoleApplication);
        var runResult = driver.GetRunResult();

        var generated = runResult.Results.SelectMany(r => r.GeneratedSources).ToArray();
        _ = generated.Select(g => g.HintName).Should().NotContain(h => h.Contains("ResourceExtensions_", StringComparison.Ordinal));
        _ = runResult.Diagnostics.Select(d => d.Id).Should().Contain("DNRESGEN007");
    }
}
