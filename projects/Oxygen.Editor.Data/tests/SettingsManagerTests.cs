// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using DryIoc;
using FluentAssertions;

namespace Oxygen.Editor.Data.Tests;

/// <summary>
/// Contains unit tests for verifying the SettingsManager functionality.
/// </summary>
[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory("Settings Manager")]
public class SettingsManagerTests : DatabaseTests
{
    public SettingsManagerTests()
    {
        this.Container.Register<SettingsManager>(Reuse.Scoped);
    }

    [TestMethod]
    public async Task SaveSettingAsync_ShouldSaveAndRetrieveSetting()
    {
        var scope = this.Container.OpenScope();
        await using (scope.ConfigureAwait(false))
        {
            var settingsManager = scope.Resolve<SettingsManager>();

            const string moduleName = "TestModule";
            const string key = "TestKey";
            var value = new { Setting = "Value" };

            await settingsManager.SaveSettingAsync(moduleName, key, value).ConfigureAwait(false);
            var retrievedValue = await settingsManager.LoadSettingAsync<object>(moduleName, key).ConfigureAwait(false);

            _ = retrievedValue.Should().NotBeNull();
            _ = retrievedValue.Should().BeEquivalentTo(value);
        }
    }

    [TestMethod]
    public async Task SaveSettingAsync_ShouldUpdateExistingSetting()
    {
        var scope = this.Container.OpenScope();
        await using (scope.ConfigureAwait(false))
        {
            var settingsManager = scope.Resolve<SettingsManager>();

            const string moduleName = "TestModule";
            const string key = "TestKey";
            var initialValue = new { Setting = "InitialValue" };
            var updatedValue = new { Setting = "UpdatedValue" };

            await settingsManager.SaveSettingAsync(moduleName, key, initialValue).ConfigureAwait(false);
            await settingsManager.SaveSettingAsync(moduleName, key, updatedValue).ConfigureAwait(false);
            var retrievedValue = await settingsManager.LoadSettingAsync<object>(moduleName, key).ConfigureAwait(false);

            _ = retrievedValue.Should().NotBeNull();
            _ = retrievedValue.Should().BeEquivalentTo(updatedValue);
        }
    }

    [TestMethod]
    public async Task LoadSettingAsync_ShouldReturnNullForNonExistentSetting()
    {
        var scope = this.Container.OpenScope();
        await using (scope.ConfigureAwait(false))
        {
            var settingsManager = scope.Resolve<SettingsManager>();

            const string moduleName = "NonExistentModule";
            const string key = "NonExistentKey";

            var retrievedValue = await settingsManager.LoadSettingAsync<object>(moduleName, key).ConfigureAwait(false);

            _ = retrievedValue.Should().BeNull();
        }
    }

    [TestMethod]
    public async Task LoadSettingAsync_ShouldReturnDefaultValueForNonExistentSetting()
    {
        var scope = this.Container.OpenScope();
        await using (scope.ConfigureAwait(false))
        {
            var settingsManager = scope.Resolve<SettingsManager>();

            const string moduleName = "NonExistentModule";
            const string key = "NonExistentKey";
            var defaultValue = new { Setting = "DefaultValue" };

            var retrievedValue = await settingsManager.LoadSettingAsync(moduleName, key, defaultValue).ConfigureAwait(false);

            _ = retrievedValue.Should().BeEquivalentTo(defaultValue);
        }
    }

    [TestMethod]
    public async Task SaveSettingAsync_ShouldHandleNullValue()
    {
        var scope = this.Container.OpenScope();
        await using (scope.ConfigureAwait(false))
        {
            var settingsManager = scope.Resolve<SettingsManager>();

            const string moduleName = "TestModule";
            const string key = "TestKey";
            object? value = null;

            await settingsManager.SaveSettingAsync(moduleName, key, value).ConfigureAwait(false);
            var retrievedValue = await settingsManager.LoadSettingAsync<object>(moduleName, key).ConfigureAwait(false);

            _ = retrievedValue.Should().BeNull();
        }
    }

    [TestMethod]
    public void RegisterChangeHandler_ShouldInvokeHandlerOnSettingChange()
    {
        var scope = this.Container.OpenScope();
        using (scope)
        {
            var settingsManager = scope.Resolve<SettingsManager>();

            const string moduleName = "TestModule";
            const string key = "TestKey";
            var value = new { Setting = "Value" };
            var handlerCalled = false;

            settingsManager.RegisterChangeHandler(moduleName, key, _ => handlerCalled = true);

            settingsManager.SaveSettingAsync(moduleName, key, value).Wait();

            _ = handlerCalled.Should().BeTrue();
        }
    }

    [TestMethod]
    public void RegisterChangeHandler_ShouldNotInvokeHandlerForDifferentSetting()
    {
        var scope = this.Container.OpenScope();
        using (scope)
        {
            var settingsManager = scope.Resolve<SettingsManager>();

            const string moduleName = "TestModule";
            const string key = "TestKey";
            const string differentKey = "DifferentKey";
            var value = new { Setting = "Value" };
            var handlerCalled = false;

            settingsManager.RegisterChangeHandler(moduleName, key, _ => handlerCalled = true);

            settingsManager.SaveSettingAsync(moduleName, differentKey, value).Wait();

            _ = handlerCalled.Should().BeFalse();
        }
    }

    [TestMethod]
    public async Task SaveAndLoadSettingAsync_ShouldHandleIntValue()
    {
        var scope = this.Container.OpenScope();
        await using (scope.ConfigureAwait(false))
        {
            var settingsManager = scope.Resolve<SettingsManager>();

            const string moduleName = "TestModule";
            const string key = "IntKey";
            const int value = 42;

            await settingsManager.SaveSettingAsync(moduleName, key, value).ConfigureAwait(false);
            settingsManager.ClearCache();
            var retrievedValue = await settingsManager.LoadSettingAsync<int>(moduleName, key).ConfigureAwait(false);

            _ = retrievedValue.Should().Be(value);
        }
    }

    [TestMethod]
    public async Task SaveAndLoadSettingAsync_ShouldHandleStringValue()
    {
        var scope = this.Container.OpenScope();
        await using (scope.ConfigureAwait(false))
        {
            var settingsManager = scope.Resolve<SettingsManager>();

            const string moduleName = "TestModule";
            const string key = "StringKey";
            const string value = "TestString";

            await settingsManager.SaveSettingAsync(moduleName, key, value).ConfigureAwait(false);
            settingsManager.ClearCache();
            var retrievedValue = await settingsManager.LoadSettingAsync<string>(moduleName, key).ConfigureAwait(false);

            _ = retrievedValue.Should().Be(value);
        }
    }

    [TestMethod]
    public async Task SaveAndLoadSettingAsync_ShouldHandleListStringValue()
    {
        var scope = this.Container.OpenScope();
        await using (scope.ConfigureAwait(false))
        {
            var settingsManager = scope.Resolve<SettingsManager>();

            const string moduleName = "TestModule";
            const string key = "ListStringKey";
            var value = new List<string> { "Value1", "Value2", "Value3" };

            await settingsManager.SaveSettingAsync(moduleName, key, value).ConfigureAwait(false);
            settingsManager.ClearCache();
            var retrievedValue = await settingsManager.LoadSettingAsync<List<string>>(moduleName, key).ConfigureAwait(false);

            _ = retrievedValue.Should().BeEquivalentTo(value);
        }
    }

    [TestMethod]
    public async Task SaveAndLoadSettingAsync_ShouldHandleDictionaryValue()
    {
        var scope = this.Container.OpenScope();
        await using (scope.ConfigureAwait(false))
        {
            var settingsManager = scope.Resolve<SettingsManager>();

            const string moduleName = "TestModule";
            const string key = "DictionaryKey";
            var value = new Dictionary<string, TestModel>(StringComparer.Ordinal)
            {
                { "Key1", new TestModel { Id = 1, Name = "Name1" } },
                { "Key2", new TestModel { Id = 2, Name = "Name2" } },
            };

            await settingsManager.SaveSettingAsync(moduleName, key, value).ConfigureAwait(false);
            settingsManager.ClearCache();
            var retrievedValue = await settingsManager.LoadSettingAsync<Dictionary<string, TestModel>>(moduleName, key).ConfigureAwait(false);

            _ = retrievedValue.Should().BeEquivalentTo(value);
        }
    }

    [SuppressMessage("ReSharper", "UnusedAutoPropertyAccessor.Local", Justification = "this is a test class")]
    private sealed class TestModel
    {
        public int Id { get; set; }

        public required string Name { get; set; }
    }
}
