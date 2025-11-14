// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using AwesomeAssertions;
using Microsoft.CodeAnalysis;

namespace DroidNet.Resources.Generator.Tests;

[TestClass]
[ExcludeFromCodeCoverage]
public class ResourcesGeneratorConfigurationTests
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

    [TestMethod]
    public void Respects_Global_Opt_Out_WithZero()
    {
        const string input = @"
namespace Testing;
public class Foo {}
";
        var driver = TestHelper.CreateGeneratorDriver(
            RuntimeStub + input,
            globalOptions: new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase)
            {
            { "build_property.DroidNetResources_GenerateHelper", "0" },
            });
        var runResult = driver.GetRunResult();
        _ = runResult.Results.SelectMany(r => r.GeneratedSources).Should().BeEmpty();
        _ = runResult.Diagnostics.Should().BeEmpty();
    }
}
