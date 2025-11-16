// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using AwesomeAssertions;
using DryIoc;

namespace Oxygen.Editor.Data.Tests;

[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory("Settings Scope")]
public class SettingsScopeTests : DatabaseTests
{
    public SettingsScopeTests()
    {
        this.Container.Register<EditorSettingsManager>(Reuse.Scoped);
    }

    [TestMethod]
    public async Task ResolveSettingAsync_ShouldReturnProjectOverrideWhenPresent()
    {
        var scope = this.Container.OpenScope();
        await using (scope.ConfigureAwait(false))
        {
            var manager = scope.Resolve<EditorSettingsManager>();
            var key = new Settings.SettingKey<string>("TestModule", "Theme");

            await manager.SaveSettingAsync(key, "ApplicationValue", Data.Settings.SettingContext.Application()).ConfigureAwait(false);
            await manager.SaveSettingAsync(key, "ProjectValue", Data.Settings.SettingContext.Project("projectA")).ConfigureAwait(false);

            var resolved = await manager.ResolveSettingAsync(key, projectId: "projectA").ConfigureAwait(false);
            _ = resolved.Should().Be("ProjectValue");
        }
    }

    [TestMethod]
    public async Task ResolveSettingAsync_ShouldFallbackToApplicationIfProjectNotPresent()
    {
        var scope = this.Container.OpenScope();
        await using (scope.ConfigureAwait(false))
        {
            var manager = scope.Resolve<EditorSettingsManager>();
            var key = new Settings.SettingKey<string>("TestModule", "Theme2");

            await manager.SaveSettingAsync(key, "ApplicationOnly", Data.Settings.SettingContext.Application()).ConfigureAwait(false);

            var resolved = await manager.ResolveSettingAsync(key, projectId: "projectB").ConfigureAwait(false);
            _ = resolved.Should().Be("ApplicationOnly");
        }
    }

    [TestMethod]
    public async Task GetDefinedScopesAsync_ShouldReturnCorrectScopes()
    {
        var scope = this.Container.OpenScope();
        await using (scope.ConfigureAwait(false))
        {
            var manager = scope.Resolve<EditorSettingsManager>();
            var key = new Settings.SettingKey<string>("TestModule", "Theme3");

            await manager.SaveSettingAsync(key, "ApplicationValue", Data.Settings.SettingContext.Application()).ConfigureAwait(false);
            await manager.SaveSettingAsync(key, "ProjectValue", Data.Settings.SettingContext.Project("projectC")).ConfigureAwait(false);

            var scopesForProject = await manager.GetDefinedScopesAsync(key, projectId: "projectC").ConfigureAwait(false);
            _ = scopesForProject.Should().Contain(Data.Settings.SettingScope.Application).And.Contain(Data.Settings.SettingScope.Project);

            var scopesForOtherProject = await manager.GetDefinedScopesAsync(key, projectId: "projectD").ConfigureAwait(false);
            _ = scopesForOtherProject.Should().Contain(Data.Settings.SettingScope.Application).And.NotContain(Data.Settings.SettingScope.Project);
        }
    }
}
