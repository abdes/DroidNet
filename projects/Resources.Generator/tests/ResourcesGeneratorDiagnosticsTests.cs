// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using System.Globalization;
using System.Security.Cryptography;
using System.Text;

using AwesomeAssertions;
using Microsoft.CodeAnalysis;

namespace DroidNet.Resources.Generator.Tests;

[TestClass]
[ExcludeFromCodeCoverage]
public class ResourcesGeneratorDiagnosticsTests
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
    /// Ensures that if the generator throws an exception, it is reported as a diagnostic
    /// (DNRESGEN001) containing the exception message and no sources are produced, meeting
    /// the requirement to surface internal generator failures safely.
    /// </summary>
    [TestMethod]
    public void Reports_Diagnostic_When_Generator_Throws()
    {
        const string input = @"
namespace Demo;

public sealed class Widget {}
";

        var driver = TestHelper.CreateGeneratorDriver(
            RuntimeStub + input,
            analyzerConfigOptionsProvider: TestHelper.CreateThrowingOptionsProvider("Boom"));

        var runResult = driver.GetRunResult();
        var diagnostics = runResult.Diagnostics.ToArray();
        _ = diagnostics.Should().NotBeEmpty();
        var failure = diagnostics.Single(d => string.Equals(d.Id, "DNRESGEN001", StringComparison.Ordinal));
        _ = failure.GetMessage(CultureInfo.InvariantCulture).Contains("Boom", StringComparison.Ordinal).Should().BeTrue("Diagnostic should include the thrown exception message");
        var generated = runResult.Results.SelectMany(r => r.GeneratedSources).ToArray();
        _ = generated.Should().BeEmpty();
    }

    [TestMethod]
    public void Reports_Regenerating_On_Generated_Localized()
    {
        const string assemblyName = "Testing";
        static string ComputeHash(string input)
        {
            var h = SHA256.HashData(Encoding.UTF8.GetBytes(input));
            var sb = new StringBuilder(8);
            for (var i = 0; i < 4; i++)
            {
                sb.Append(h[i].ToString("x2", CultureInfo.InvariantCulture));
            }

            return sb.ToString();
        }

        var assemblyHash = ComputeHash(assemblyName);
        var generatedNamespace = $"DroidNet.Resources.Generator.Localized_{assemblyHash}";
        var input = $"namespace {generatedNamespace};\n\n[System.CodeDom.Compiler.GeneratedCodeAttribute(\"Manual\",\"1.0.0\")]\ninternal static class Localized {{}}\n";

        var driver = TestHelper.CreateGeneratorDriver(RuntimeStub + input, assemblyName: assemblyName);
        var runResult = driver.GetRunResult();

        // Verify local helper was still generated
        var generated = runResult.Results.SelectMany(r => r.GeneratedSources).ToArray();
        _ = generated.Length.Should().BePositive();

        // Verify Regenerating diagnostic is present (DNRESGEN005)
        _ = runResult.Diagnostics.Select(d => d.Id).Should().Contain("DNRESGEN005");
    }
}
