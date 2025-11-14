// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using System.Globalization;

using AwesomeAssertions;
using Microsoft.CodeAnalysis;

namespace DroidNet.Resources.Generator.Tests;

[TestClass]
[ExcludeFromCodeCoverage]
public class ResourcesGeneratorActivationTests
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
    /// Verifies that when the runtime helper symbols (ResourceExtensions) are present in the
    /// compilation, the generator produces the localized helper (L extension), fulfilling the
    /// requirement to generate localization helpers only when the runtime API is available.
    /// </summary>
    [TestMethod]
    public void Generates_Localized_Helper_When_RuntimeSymbolPresent()
    {
        const string input = @"
namespace Testing;

public class Foo {}
";

        var driver = TestHelper.CreateGeneratorDriver(RuntimeStub + input);
        var runResult = driver.GetRunResult();

        // Ensure the generator produced at least one generated source
        var generated = runResult.Results.SelectMany(r => r.GeneratedSources).ToArray();
        _ = generated.Length.Should().BePositive("Expected generated sources to be present");

        // Assert that one of the generated sources contains the L extension
        _ = generated.Select(gs => gs.SourceText.ToString()).Should().Contain(src => src.Contains("public static string L(this string value", StringComparison.Ordinal));
    }

    /// <summary>
    /// Ensures the generator skips producing helper sources when the runtime ResourceExtensions
    /// symbol is missing and reports an informational diagnostic (DNRESGEN002), meeting the
    /// requirement to avoid generation without the runtime support.
    /// </summary>
    [TestMethod]
    public void Skips_Generation_When_Runtime_Symbol_Is_Missing()
    {
        const string input = @"
namespace Testing;

public sealed class Bar {}
";

        var driver = TestHelper.CreateGeneratorDriver(input);
        var runResult = driver.GetRunResult();

        var generated = runResult.Results.SelectMany(r => r.GeneratedSources).ToArray();
        _ = generated.Should().BeEmpty();

        var diagnostics = runResult.Diagnostics.ToArray();
        _ = diagnostics.Length.Should().BePositive("Expected informational diagnostic when runtime type is missing");
        var info = diagnostics.Single(d => string.Equals(d.Id, "DNRESGEN002", StringComparison.Ordinal));
        var msg = info.GetMessage(CultureInfo.InvariantCulture);
        _ = msg.Should().Contain("not found", "Expected informative diagnostic that the runtime ResourceExtensions symbol was not found");
    }

    /// <summary>
    /// Confirms the generator respects the global analyzer config opt-out property
    /// `DroidNetResources_GenerateHelper = false` by producing no sources and no diagnostics,
    /// satisfying the requirement to allow global opt-out.
    /// </summary>
    [TestMethod]
    public void Respects_Global_Opt_Out_Property()
    {
        const string input = @"
namespace Testing;

public sealed class Bar {}
";

        var driver = TestHelper.CreateGeneratorDriver(
            RuntimeStub + input,
            globalOptions: new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase)
            {
                { "build_property.DroidNetResources_GenerateHelper", "false" },
            });

        var runResult = driver.GetRunResult();
        var generated = runResult.Results.SelectMany(r => r.GeneratedSources).ToArray();
        _ = generated.Should().BeEmpty();
        _ = runResult.Diagnostics.Should().BeEmpty();
    }

    /// <summary>
    /// Verifies that an invalid opt-out value is ignored and the generator still runs,
    /// emitting the expected informational diagnostic (DNRESGEN002), meeting the requirement
    /// for robust configuration parsing.
    /// </summary>
    [TestMethod]
    public void Ignores_Invalid_Global_Opt_Out_Property()
    {
        const string input = @"
namespace Testing;

public sealed class Bar {}
";

        var driver = TestHelper.CreateGeneratorDriver(
            RuntimeStub + input,
            globalOptions: new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase)
            {
                { "build_property.DroidNetResources_GenerateHelper", "maybe" },
            });

        var runResult = driver.GetRunResult();
        var generated = runResult.Results.SelectMany(r => r.GeneratedSources).ToArray();
        _ = generated.Length.Should().BePositive("Generator should run when the opt-out value is invalid");
        _ = runResult.Diagnostics.Select(d => d.Id).Should().Contain("DNRESGEN002");
    }
}
