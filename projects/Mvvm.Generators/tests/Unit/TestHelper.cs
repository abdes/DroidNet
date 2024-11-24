// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using Microsoft.CodeAnalysis;
using Microsoft.CodeAnalysis.CSharp;

namespace DroidNet.Mvvm.Generators.Tests;

/// <summary>
/// Helpers for the test cases.
/// </summary>
[ExcludeFromCodeCoverage]
public static class TestHelper
{
    /// <summary>
    /// Creates and runs a driver for the <see cref="ViewModelWiringGenerator" />.
    /// </summary>
    /// <param name="source">The input source for the generator.</param>
    /// <returns>The generator driver which results can be verified.</returns>
    public static GeneratorDriver GeneratorDriver(string source)
    {
        // Parse the provided string into a C# syntax tree
        var syntaxTree = CSharpSyntaxTree.ParseText(source);

        // Create references for assemblies we require. All basic net80, plus
        // the assembly containing the ViewModelAttribute class.
        IEnumerable<PortableExecutableReference> references =
        [
            .. Basic.Reference.Assemblies.Net80.References.All,
            MetadataReference.CreateFromFile(typeof(ViewModelAttribute).Assembly.Location),
        ];

        // Create a Roslyn compilation for the syntax tree.
        var compilation = CSharpCompilation.Create(
            "Tests",
            [syntaxTree],
            references,
            new CSharpCompilationOptions(OutputKind.DynamicallyLinkedLibrary));

        // Create an instance of our EnumGenerator incremental source generator
        var generator = new ViewModelWiringGenerator();

        // The GeneratorDriver is used to run our generator against a compilation
        GeneratorDriver driver = CSharpGeneratorDriver.Create(generator);

        // Run the source generator!
        return driver.RunGenerators(compilation);
    }
}
