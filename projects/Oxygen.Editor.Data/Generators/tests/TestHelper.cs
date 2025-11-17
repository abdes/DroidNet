// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using Microsoft.CodeAnalysis;
using Microsoft.CodeAnalysis.CSharp;
using Microsoft.CodeAnalysis.Diagnostics;

namespace Oxygen.Editor.Data.Generators.Tests;

[ExcludeFromCodeCoverage]
internal static class TestHelper
{
    public static GeneratorDriver CreateGeneratorDriver(
        string source,
        string? assemblyName = null,
        IReadOnlyDictionary<string, string>? globalOptions = null,
        AnalyzerConfigOptionsProvider? analyzerConfigOptionsProvider = null,
        OutputKind outputKind = OutputKind.DynamicallyLinkedLibrary,
        IEnumerable<PortableExecutableReference>? additionalReferences = null)
    {
        var syntaxTree = CSharpSyntaxTree.ParseText(source);

        IEnumerable<PortableExecutableReference> references = Basic.Reference.Assemblies.Net80.References.All;
        if (additionalReferences != null)
        {
            references = references.Concat(additionalReferences);
        }

        var compilation = CSharpCompilation.Create(
            assemblyName ?? "Tests",
            [syntaxTree],
            references,
            new CSharpCompilationOptions(outputKind));

        var generator = new SettingDescriptorGenerator();
        GeneratorDriver driver = CSharpGeneratorDriver.Create([generator]);

        if (analyzerConfigOptionsProvider != null)
        {
            driver = driver.WithUpdatedAnalyzerConfigOptions(analyzerConfigOptionsProvider);
        }
        else if (globalOptions != null)
        {
            driver = driver.WithUpdatedAnalyzerConfigOptions(new InMemoryAnalyzerConfigOptionsProvider(globalOptions));
        }

        return driver.RunGenerators(compilation);
    }

    public static AnalyzerConfigOptionsProvider CreateThrowingOptionsProvider(string message) => new ThrowingAnalyzerConfigOptionsProvider(message);

    private sealed class InMemoryAnalyzerConfigOptionsProvider : AnalyzerConfigOptionsProvider
    {
        internal InMemoryAnalyzerConfigOptionsProvider(IReadOnlyDictionary<string, string> globalOptions)
        {
            this.GlobalOptions = new InMemoryAnalyzerConfigOptions(globalOptions);
        }

        public override AnalyzerConfigOptions GlobalOptions { get; }

        public override AnalyzerConfigOptions GetOptions(SyntaxTree tree) => InMemoryAnalyzerConfigOptions.Empty;

        public override AnalyzerConfigOptions GetOptions(AdditionalText textFile) => InMemoryAnalyzerConfigOptions.Empty;
    }

    private sealed class InMemoryAnalyzerConfigOptions : AnalyzerConfigOptions
    {
        private readonly Dictionary<string, string> options;

        internal InMemoryAnalyzerConfigOptions(IReadOnlyDictionary<string, string> options)
        {
            this.options = new Dictionary<string, string>(options, StringComparer.OrdinalIgnoreCase);
        }

        internal static AnalyzerConfigOptions Empty { get; } = new InMemoryAnalyzerConfigOptions(new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase));

        public override bool TryGetValue(string key, out string value) => this.options.TryGetValue(key, out value!);
    }

    private sealed class ThrowingAnalyzerConfigOptionsProvider : AnalyzerConfigOptionsProvider
    {
        internal ThrowingAnalyzerConfigOptionsProvider(string message)
        {
            this.GlobalOptions = new ThrowingAnalyzerConfigOptions(message);
        }

        public override AnalyzerConfigOptions GlobalOptions { get; }

        public override AnalyzerConfigOptions GetOptions(SyntaxTree tree)
            => InMemoryAnalyzerConfigOptions.Empty;

        public override AnalyzerConfigOptions GetOptions(AdditionalText textFile)
            => InMemoryAnalyzerConfigOptions.Empty;
    }

    private sealed class ThrowingAnalyzerConfigOptions : AnalyzerConfigOptions
    {
        private readonly string message;

        internal ThrowingAnalyzerConfigOptions(string message)
        {
            this.message = message;
        }

        public override bool TryGetValue(string key, out string value)
            => throw new InvalidOperationException(this.message);
    }
}
