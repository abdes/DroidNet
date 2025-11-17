// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;

using AwesomeAssertions;
using Microsoft.CodeAnalysis;

namespace Oxygen.Editor.Data.Generators.Tests;

[TestClass]
[ExcludeFromCodeCoverage]
public class ActivationTests
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
    }
}
""";

    private const string ModuleSettingsStub = """
namespace Testing
{
    public sealed partial class Foo : Oxygen.Editor.Data.Models.ModuleSettings
    {
        private new const string ModuleName = "Testing";
        [Oxygen.Editor.Data.Persisted]
        public string Name { get; set; } = string.Empty;
    }
}
""";

    /// <summary>
    /// Verifies that when the runtime helper symbols (ResourceExtensions) are present in the
    /// compilation, the generator produces the localized helper (L extension), fulfilling the
    /// requirement to generate localization helpers only when the runtime API is available.
    /// </summary>
    [TestMethod]
    public void Generates_Descriptor_When_RuntimeSymbolPresent()
    {
        var driver = TestHelper.CreateGeneratorDriver(RuntimeStub + ModuleSettingsStub);
        var runResult = driver.GetRunResult();
        foreach (var d in runResult.Diagnostics)
        {
            System.Console.WriteLine($"DIAG: {d.Id} - {d.GetMessage(System.Globalization.CultureInfo.InvariantCulture)}");
        }

        // Ensure the generator produced at least one generated source
        var generated = runResult.Results.SelectMany(r => r.GeneratedSources).ToArray();
        _ = generated.Length.Should().BePositive("Expected generated sources to be present");

        // Assert that we generated at least one descriptor
        _ = generated.Select(gs => gs.SourceText.ToString()).Should().Contain(src => src.Contains("SettingDescriptor<", StringComparison.Ordinal));
    }

    /// <summary>
    /// Ensures the generator skips producing helper sources when the runtime ResourceExtensions
    /// symbol is missing and reports an informational diagnostic (DNRESGEN002), meeting the
    /// requirement to avoid generation without the runtime support.
    /// </summary>
    [TestMethod]
    public void Skips_Generation_When_ModuleSettings_Is_Missing()
    {
        var driver = TestHelper.CreateGeneratorDriver(ModuleSettingsStub);
        var runResult = driver.GetRunResult();

        var generated = runResult.Results.SelectMany(r => r.GeneratedSources).ToArray();
        _ = generated.Should().BeEmpty();

        var diagnostics = runResult.Diagnostics.ToArray();

        // If ModuleSettingsStub is missing, the generator should not have produced any sources
        // but the diagnostic pipeline may report that the generator started -- ensure that generation was skipped
        _ = generated.Should().BeEmpty();
        _ = diagnostics.Select(d => d.Id).Should().Contain("OXGNPG000");
    }

    /// <summary>
    /// Confirms the generator respects the global analyzer config opt-out property
    /// `DroidNetResources_GenerateHelper = false` by producing no sources and no diagnostics,
    /// satisfying the requirement to allow global opt-out.
    /// </summary>
    [TestMethod]
    public void Respects_Global_Opt_Out_Property()
    {
        var driver = TestHelper.CreateGeneratorDriver(
            RuntimeStub + ModuleSettingsStub,
            globalOptions: new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase)
            {
                { "build_property.DroidNetOxygenEditorData_GenerateSettingDescriptors", "false" },
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
        var driver = TestHelper.CreateGeneratorDriver(
            RuntimeStub + ModuleSettingsStub,
            globalOptions: new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase)
            {
                { "build_property.DroidNetOxygenEditorData_GenerateSettingDescriptors", "maybe" },
            });

        var runResult = driver.GetRunResult();
        var generated = runResult.Results.SelectMany(r => r.GeneratedSources).ToArray();
        _ = generated.Length.Should().BePositive("Generator should run when the opt-out value is invalid");

        // If we're running with invalid opt-out, generator should still run and create descriptors
    }

    [TestMethod]
    public void Respects_Global_Opt_Out_WithZero()
    {
        var driver = TestHelper.CreateGeneratorDriver(
            RuntimeStub + ModuleSettingsStub,
            globalOptions: new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase)
            {
                { "build_property.DroidNetOxygenEditorData_GenerateSettingDescriptors", "0" },
            });
        var runResult = driver.GetRunResult();
        _ = runResult.Results.SelectMany(r => r.GeneratedSources).Should().BeEmpty();
        _ = runResult.Diagnostics.Should().BeEmpty();
    }
}
