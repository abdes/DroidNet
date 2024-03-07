// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Hosting.Generators;

using System.Diagnostics.CodeAnalysis;
using Microsoft.CodeAnalysis;
using Microsoft.CodeAnalysis.CSharp;
using Microsoft.Extensions.DependencyInjection;

/// <summary>
/// Helpers for the test cases.
/// </summary>
[ExcludeFromCodeCoverage]
public static class TestHelper
{
    /// <summary>
    /// Creates and runs a driver for the <see cref="AutoInjectGenerator" />.
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
            MetadataReference.CreateFromFile(typeof(ServiceLifetime).Assembly.Location),
            MetadataReference.CreateFromFile(typeof(InjectAsAttribute).Assembly.Location),
        ];

        // Create a Roslyn compilation for the syntax tree.
        var compilation = CSharpCompilation.Create(
            "Tests",
            new[] { syntaxTree },
            references,
            new CSharpCompilationOptions(OutputKind.DynamicallyLinkedLibrary));

        // Create an instance of our EnumGenerator incremental source generator
        var generator = new AutoInjectGenerator();

        // The GeneratorDriver is used to run our generator against a compilation
        GeneratorDriver driver = CSharpGeneratorDriver.Create(generator);

        // Run the source generator!
        return driver.RunGenerators(compilation);
    }
}
