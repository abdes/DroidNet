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
    public async Task GetDefinedScopesAsync_ShouldReturnCorrectScopes()
    {
        var scope = this.Container.OpenScope();
        await using (scope.ConfigureAwait(false))
        {
            var manager = scope.Resolve<EditorSettingsManager>();
            var key = new Settings.SettingKey<string?>("TestModule", "Theme3");

            await manager.SaveSettingAsync(key, "ApplicationValue", Settings.SettingContext.Application(), ct: this.CancellationToken).ConfigureAwait(false);
            await manager.SaveSettingAsync(key, "ProjectValue", Settings.SettingContext.Project("projectC"), ct: this.CancellationToken).ConfigureAwait(false);

            var scopesForProject = await manager.GetDefinedScopesAsync(key, projectId: "projectC", this.CancellationToken).ConfigureAwait(false);
            _ = scopesForProject.Should().Contain(Settings.SettingScope.Application).And.Contain(Settings.SettingScope.Project);

            var scopesForOtherProject = await manager.GetDefinedScopesAsync(key, projectId: "projectD", this.CancellationToken).ConfigureAwait(false);
            _ = scopesForOtherProject.Should().Contain(Settings.SettingScope.Application).And.NotContain(Settings.SettingScope.Project);
        }
    }

    [TestMethod]
    public async Task GetDefinedScopesAsync_UnknownKey_ReturnsEmpty()
    {
        var scope = this.Container.OpenScope();
        await using (scope.ConfigureAwait(false))
        {
            var manager = scope.Resolve<EditorSettingsManager>();
            var key = new Settings.SettingKey<string?>("NonExistentModule", "Nobody knows");

            var scopes = await manager.GetDefinedScopesAsync(key, projectId: null, this.CancellationToken).ConfigureAwait(false);
            _ = scopes.Should().BeEmpty();
        }
    }
}
