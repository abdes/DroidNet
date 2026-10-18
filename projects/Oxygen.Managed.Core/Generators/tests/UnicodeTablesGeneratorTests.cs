// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using AwesomeAssertions;
using Microsoft.CodeAnalysis;
using Microsoft.CodeAnalysis.CSharp;
using Microsoft.CodeAnalysis.Text;

namespace Oxygen.Managed.Core.Generators.Tests;

/// <summary>Checks the tables generated from the Unicode database and the diagnostics of a bad database.</summary>
[TestClass]
[ExcludeFromCodeCoverage]
public sealed class UnicodeTablesGeneratorTests
{
    private const string Age = """
        # DerivedAge-15.1.0.txt
        0041..005A    ; 1.1 # Latin capitals
        0030..0039    ; 1.1 # digits
        005B          ; 1.1
        00DF          ; 1.1
        """;

    private const string Folding = """
        # CaseFolding-15.1.0.txt
        0041; C; 0061; # A
        00DF; F; 0073 0073; # sharp s
        1E9E; F; 0073 0073; # capital sharp s
        1E9E; S; 00DF; # capital sharp s
        0130; T; 0069; # dotted I
        """;

    [TestMethod]
    public void Generate_Database_MergesRangesAndKeepsSimpleFoldings()
    {
        var result = Run(("DerivedAge.txt", Age), ("CaseFolding.txt", Folding));

        _ = result.Diagnostics.Should().BeEmpty();
        var source = result.GeneratedSources.Should().ContainSingle().Subject.SourceText.ToString().ReplaceLineEndings("\n");
        _ = source.Should().Contain("public const string Version = \"15.1.0\";");
        _ = source.Should().Contain("AssignedFirst =>\n    [\n        0x000030, 0x000041, 0x0000DF,");
        _ = source.Should().Contain("AssignedLast =>\n    [\n        0x000039, 0x00005B, 0x0000DF,");
        _ = source.Should().Contain("FoldingCodes =>\n    [\n        0x000041, 0x001E9E,");
        _ = source.Should().Contain("FoldingTargets =>\n    [\n        0x000061, 0x0000DF,");
    }

    [TestMethod]
    public void Generate_MissingFile_ReportsMissingDatabase()
    {
        var result = Run(("DerivedAge.txt", Age));

        _ = result.Diagnostics.Select(static diagnostic => diagnostic.Id).Should().Equal("OXGNUT001");
        _ = result.GeneratedSources.Should().BeEmpty();
    }

    [TestMethod]
    public void Generate_MixedVersions_ReportsInvalidDatabase()
    {
        var result = Run(("DerivedAge.txt", Age), ("CaseFolding.txt", Folding.Replace("15.1.0", "16.0.0", StringComparison.Ordinal)));

        _ = result.Diagnostics.Select(static diagnostic => diagnostic.Id).Should().Equal("OXGNUT002");
        _ = result.GeneratedSources.Should().BeEmpty();
    }

    private static GeneratorRunResult Run(params (string Name, string Text)[] files)
    {
        var compilation = CSharpCompilation.Create(
            "Tests",
            [],
            Basic.Reference.Assemblies.Net80.References.All,
            new CSharpCompilationOptions(OutputKind.DynamicallyLinkedLibrary));
        var driver = CSharpGeneratorDriver.Create(
            [new UnicodeTablesGenerator().AsSourceGenerator()],
            [.. files.Select(static file => (AdditionalText)new InMemoryText("C:/ucd/" + file.Name, file.Text.Replace("\r\n", "\n", StringComparison.Ordinal)))]);
        return driver.RunGenerators(compilation).GetRunResult().Results.Single();
    }

    private sealed class InMemoryText(string path, string text) : AdditionalText
    {
        public override string Path { get; } = path;

        public override SourceText GetText(CancellationToken cancellationToken = default) => SourceText.From(text);
    }
}
