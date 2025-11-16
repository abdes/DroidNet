// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using AwesomeAssertions;
using DryIoc;

namespace Oxygen.Editor.Data.Tests;

/// <summary>
/// Contains unit tests for verifying the EditorSettingsManager functionality.
/// </summary>
[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory("Settings Manager")]
public class EditorSettingsManagerTests : DatabaseTests
{
    public EditorSettingsManagerTests()
    {
        this.Container.Register<EditorSettingsManager>(Reuse.Scoped);
    }

    public TestContext TestContext { get; set; }

    [TestMethod]
    public async Task SaveSettingAsync_ShouldSaveAndRetrieveSetting()
    {
        var scope = this.Container.OpenScope();
        await using (scope.ConfigureAwait(false))
        {
            var settingsManager = scope.Resolve<EditorSettingsManager>();

            const string moduleName = "TestModule";
            const string key = "TestKey";
            var settingKey = new Settings.SettingKey<object?>(moduleName, key);
            var value = new { Setting = "Value" };

            await settingsManager.SaveSettingAsync(settingKey, value).ConfigureAwait(false);
            var retrievedValue = await settingsManager.LoadSettingAsync(settingKey).ConfigureAwait(false);

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
            var settingsManager = scope.Resolve<EditorSettingsManager>();

            const string moduleName = "TestModule";
            const string key = "TestKey";
            var settingKey = new Settings.SettingKey<object?>(moduleName, key);
            var initialValue = new { Setting = "InitialValue" };
            var updatedValue = new { Setting = "UpdatedValue" };

            await settingsManager.SaveSettingAsync(settingKey, initialValue).ConfigureAwait(false);
            await settingsManager.SaveSettingAsync(settingKey, updatedValue).ConfigureAwait(false);
            var retrievedValue = await settingsManager.LoadSettingAsync(settingKey).ConfigureAwait(false);

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
            var settingsManager = scope.Resolve<EditorSettingsManager>();

            const string moduleName = "NonExistentModule";
            const string key = "NonExistentKey";

            var retrievedValue = await settingsManager.LoadSettingAsync(new Settings.SettingKey<object?>(moduleName, key)).ConfigureAwait(false);

            _ = retrievedValue.Should().BeNull();
        }
    }

    [TestMethod]
    public async Task LoadSettingAsync_ShouldReturnDefaultValueForNonExistentSetting()
    {
        var scope = this.Container.OpenScope();
        await using (scope.ConfigureAwait(false))
        {
            var settingsManager = scope.Resolve<EditorSettingsManager>();

            const string moduleName = "NonExistentModule";
            const string key = "NonExistentKey";
            var defaultValue = new { Setting = "DefaultValue" };

            var retrievedValue = await settingsManager.LoadSettingAsync(new Settings.SettingKey<object?>(moduleName, key), defaultValue).ConfigureAwait(false);

            _ = retrievedValue.Should().BeEquivalentTo(defaultValue);
        }
    }

    [TestMethod]
    public async Task SaveSettingAsync_ShouldHandleNullValue()
    {
        var scope = this.Container.OpenScope();
        await using (scope.ConfigureAwait(false))
        {
            var settingsManager = scope.Resolve<EditorSettingsManager>();

            const string moduleName = "TestModule";
            const string key = "TestKey";
            object value = null!;

            await settingsManager.SaveSettingAsync(new Settings.SettingKey<object>(moduleName, key), value).ConfigureAwait(false);
            var retrievedValue = await settingsManager.LoadSettingAsync(new Settings.SettingKey<object?>(moduleName, key)).ConfigureAwait(false);

            _ = retrievedValue.Should().BeNull();
        }
    }

    [TestMethod]
    public void RegisterChangeHandler_ShouldInvokeHandlerOnSettingChange()
    {
        var scope = this.Container.OpenScope();
        using (scope)
        {
            var settingsManager = scope.Resolve<EditorSettingsManager>();

            const string moduleName = "TestModule";
            const string key = "TestKey";
            var value = new { Setting = "Value" };
            var handlerCalled = false;

            var subject = settingsManager.WhenSettingChanged(new Settings.SettingKey<object?>(moduleName, key));
            using var subscription = subject.Subscribe(evt => handlerCalled = true);
            settingsManager.SaveSettingAsync(new Settings.SettingKey<object?>(moduleName, key), value).Wait(this.TestContext.CancellationToken);

            _ = handlerCalled.Should().BeTrue();
        }
    }

    [TestMethod]
    public void RegisterChangeHandler_ShouldNotInvokeHandlerForDifferentSetting()
    {
        var scope = this.Container.OpenScope();
        using (scope)
        {
            var settingsManager = scope.Resolve<EditorSettingsManager>();

            const string moduleName = "TestModule";
            const string key = "TestKey";
            const string differentKey = "DifferentKey";
            var value = new { Setting = "Value" };
            var handlerCalled = false;

            using var subscription = settingsManager.WhenSettingChanged(new Settings.SettingKey<object?>(moduleName, key)).Subscribe(evt => handlerCalled = true);

            settingsManager.SaveSettingAsync(new Settings.SettingKey<object?>(moduleName, differentKey), value).Wait(this.TestContext.CancellationToken);

            _ = handlerCalled.Should().BeFalse();
        }
    }

    [TestMethod]
    public async Task SaveAndLoadSettingAsync_ShouldHandleIntValue()
    {
        var scope = this.Container.OpenScope();
        await using (scope.ConfigureAwait(false))
        {
            var settingsManager = scope.Resolve<EditorSettingsManager>();

            const string moduleName = "TestModule";
            const string key = "IntKey";
            const int value = 42;

            await settingsManager.SaveSettingAsync(new Settings.SettingKey<int>(moduleName, key), value).ConfigureAwait(false);
            settingsManager.ClearCache();
            var retrievedValue = await settingsManager.LoadSettingAsync(new Settings.SettingKey<int>(moduleName, key)).ConfigureAwait(false);

            _ = retrievedValue.Should().Be(value);
        }
    }

    [TestMethod]
    public async Task SaveAndLoadSettingAsync_ShouldHandleStringValue()
    {
        var scope = this.Container.OpenScope();
        await using (scope.ConfigureAwait(false))
        {
            var settingsManager = scope.Resolve<EditorSettingsManager>();

            const string moduleName = "TestModule";
            const string key = "StringKey";
            const string value = "TestString";

            await settingsManager.SaveSettingAsync(new Settings.SettingKey<string>(moduleName, key), value).ConfigureAwait(false);
            settingsManager.ClearCache();
            var retrievedValue = await settingsManager.LoadSettingAsync(new Settings.SettingKey<string>(moduleName, key)).ConfigureAwait(false);

            _ = retrievedValue.Should().Be(value);
        }
    }

    [TestMethod]
    public async Task SaveAndLoadSettingAsync_ShouldHandleListStringValue()
    {
        var scope = this.Container.OpenScope();
        await using (scope.ConfigureAwait(false))
        {
            var settingsManager = scope.Resolve<EditorSettingsManager>();

            const string moduleName = "TestModule";
            const string key = "ListStringKey";
            var value = new List<string> { "Value1", "Value2", "Value3" };

            await settingsManager.SaveSettingAsync(new Settings.SettingKey<List<string>>(moduleName, key), value).ConfigureAwait(false);
            settingsManager.ClearCache();
            var retrievedValue = await settingsManager.LoadSettingAsync(new Settings.SettingKey<List<string>>(moduleName, key)).ConfigureAwait(false);

            _ = retrievedValue.Should().BeEquivalentTo(value);
        }
    }

    [TestMethod]
    public async Task SaveAndLoadSettingAsync_ShouldHandleDictionaryValue()
    {
        var scope = this.Container.OpenScope();
        await using (scope.ConfigureAwait(false))
        {
            var settingsManager = scope.Resolve<EditorSettingsManager>();

            const string moduleName = "TestModule";
            const string key = "DictionaryKey";
            var value = new Dictionary<string, TestModel>(StringComparer.Ordinal)
            {
                { "Key1", new TestModel { Id = 1, Name = "Name1" } },
                { "Key2", new TestModel { Id = 2, Name = "Name2" } },
            };

            await settingsManager.SaveSettingAsync(new Settings.SettingKey<Dictionary<string, TestModel>>(moduleName, key), value).ConfigureAwait(false);
            settingsManager.ClearCache();
            var retrievedValue = await settingsManager.LoadSettingAsync(new Settings.SettingKey<Dictionary<string, TestModel>>(moduleName, key)).ConfigureAwait(false);

            _ = retrievedValue.Should().BeEquivalentTo(value);
        }
    }

    [TestMethod]
    public async Task GetDescriptorsByCategoryAsync_ShouldReturnRegisteredDescriptors()
    {
        var scope = this.Container.OpenScope();
        await using (scope.ConfigureAwait(false))
        {
            var settingsManager = scope.Resolve<EditorSettingsManager>();

            // Act: Query descriptors (should include those registered by module initializer)
            var descriptorsByCategory = await settingsManager.GetDescriptorsByCategoryAsync().ConfigureAwait(false);

            // Assert: ExampleSettings descriptors should be registered
            _ = descriptorsByCategory.Should().NotBeNull();
            _ = descriptorsByCategory.Should().ContainKey("Layout");

            var layoutDescriptors = descriptorsByCategory["Layout"];
            _ = layoutDescriptors.Should().HaveCount(2);
            _ = layoutDescriptors.Should().Contain(d => d.Name == "WindowPosition");
            _ = layoutDescriptors.Should().Contain(d => d.Name == "WindowSize");
        }
    }

    [TestMethod]
    public async Task SearchDescriptorsAsync_ShouldFindRegisteredDescriptors()
    {
        var scope = this.Container.OpenScope();
        await using (scope.ConfigureAwait(false))
        {
            var settingsManager = scope.Resolve<EditorSettingsManager>();

            // Act: Search for window-related descriptors
            var matches = await settingsManager.SearchDescriptorsAsync("Window").ConfigureAwait(false);

            // Assert: Should find WindowPosition and WindowSize
            _ = matches.Should().NotBeNull();
            _ = matches.Should().HaveCountGreaterThanOrEqualTo(2);
            _ = matches.Should().Contain(d => d.Name == "WindowPosition");
            _ = matches.Should().Contain(d => d.Name == "WindowSize");
        }
    }

    [SuppressMessage("ReSharper", "UnusedAutoPropertyAccessor.Local", Justification = "this is a test class")]
    private sealed class TestModel
    {
        public int Id { get; set; }

        public required string Name { get; set; }
    }
}
