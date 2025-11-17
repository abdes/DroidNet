// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;

using AwesomeAssertions;
using Microsoft.CodeAnalysis;

namespace Oxygen.Editor.Data.Generators.Tests;

[TestClass]
[ExcludeFromCodeCoverage]
public class DiagnosticsTests
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

    /// <summary>
    /// Ensures the generator reports an error when a member type is non-serializable
    /// (e.g., delegate) and reports an error diagnostic rather than generating sources.
    /// </summary>
    [TestMethod]
    public void Reports_Error_When_PropertyType_Is_NotSerializable()
    {
        const string input = """
namespace Testing;

public sealed partial class MySettings : Oxygen.Editor.Data.Models.ModuleSettings
{
    private new const string ModuleName = "Testing";
    [Oxygen.Editor.Data.Persisted]
    public System.Action Broken { get; set; }
}
""";

        var driver = TestHelper.CreateGeneratorDriver(RuntimeStub + input);
        var runResult = driver.GetRunResult();
        var diagnostics = runResult.Diagnostics.ToArray();
        _ = diagnostics.Select(d => d.Id).Should().Contain("OXGNPG103");
        var generated = runResult.Results.SelectMany(r => r.GeneratedSources).ToArray();
        _ = generated.Should().BeEmpty();
    }

    [TestMethod]
    public void Reports_GeneratorStarted_When_Preexisting_Generated()
    {
        const string input = """
namespace Testing;

[System.CodeDom.Compiler.GeneratedCodeAttribute("Manual","1.0.0")]
internal static class GeneratedStub {}

public sealed partial class PreExistingGeneratedSettings : Oxygen.Editor.Data.Models.ModuleSettings
{
    private new const string ModuleName = "Testing";
    [Oxygen.Editor.Data.Persisted]
    public string Name { get; set; }
}
""";

        var driver = TestHelper.CreateGeneratorDriver(RuntimeStub + input);
        var runResult = driver.GetRunResult();

        // Verify descriptors were still generated
        var generated = runResult.Results.SelectMany(r => r.GeneratedSources).ToArray();
        _ = generated.Length.Should().BePositive();

        // Verify Regenerating diagnostic is present (DNRESGEN005)
        _ = runResult.Diagnostics.Select(d => d.Id).Should().Contain("OXGNPG000");
    }

    [TestMethod]
    public void Reports_Error_When_Class_Is_Not_Partial()
    {
        const string input = """
namespace Testing;

public sealed class NotPartial : Oxygen.Editor.Data.Models.ModuleSettings
{
    private new const string ModuleName = "Testing";
    [Oxygen.Editor.Data.Persisted]
    public string Name { get; set; }
}
""";

        var driver = TestHelper.CreateGeneratorDriver(RuntimeStub + input);
        var runResult = driver.GetRunResult();
        _ = runResult.Diagnostics.Select(d => d.Id).Should().Contain("OXGNPG001");
    }

    [TestMethod]
    public void Reports_Error_When_ModuleName_Is_Missing()
    {
        const string input = """
namespace Testing;

public sealed partial class NoModuleName : Oxygen.Editor.Data.Models.ModuleSettings
{
    [Oxygen.Editor.Data.Persisted]
    public string Name { get; set; }
}
""";

        var driver = TestHelper.CreateGeneratorDriver(RuntimeStub + input);
        var runResult = driver.GetRunResult();
        _ = runResult.Diagnostics.Select(d => d.Id).Should().Contain("OXGNPG002");
    }

    [TestMethod]
    public void Reports_Error_When_ModuleName_Is_Not_Const()
    {
        const string input = """
namespace Testing;

public sealed partial class ModuleNameNotConst : Oxygen.Editor.Data.Models.ModuleSettings
{
    private new string ModuleName = "Testing";
    [Oxygen.Editor.Data.Persisted]
    public string Name { get; set; }
}
""";

        var driver = TestHelper.CreateGeneratorDriver(RuntimeStub + input);
        var runResult = driver.GetRunResult();
        _ = runResult.Diagnostics.Select(d => d.Id).Should().Contain("OXGNPG003");
    }

    [TestMethod]
    public void Reports_Warning_When_PersistedProperty_Is_Not_Public()
    {
        const string input = """
namespace Testing;

public sealed partial class PrivateProperty : Oxygen.Editor.Data.Models.ModuleSettings
{
    private new const string ModuleName = "Testing";
    [Oxygen.Editor.Data.Persisted]
    private string Hidden { get; set; }
}
""";

        var driver = TestHelper.CreateGeneratorDriver(RuntimeStub + input);
        var runResult = driver.GetRunResult();
        _ = runResult.Diagnostics.Select(d => d.Id).Should().Contain("OXGNPG101");
    }

    [TestMethod]
    public void Reports_Error_When_PersistedProperty_Missing_Setter()
    {
        const string input = """
namespace Testing;

public sealed partial class NoSetter : Oxygen.Editor.Data.Models.ModuleSettings
{
    private new const string ModuleName = "Testing";
    [Oxygen.Editor.Data.Persisted]
    public string Name { get; }
}
""";

        var driver = TestHelper.CreateGeneratorDriver(RuntimeStub + input);
        var runResult = driver.GetRunResult();
        _ = runResult.Diagnostics.Select(d => d.Id).Should().Contain("OXGNPG102");
    }
}
