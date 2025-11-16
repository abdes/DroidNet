// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using AwesomeAssertions;
using DryIoc;
using Oxygen.Editor.Data.Settings;

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

            await settingsManager.SaveSettingAsync(settingKey, value, ct: this.CancellationToken).ConfigureAwait(false);
            var retrievedValue = await settingsManager.LoadSettingAsync(settingKey, ct: this.CancellationToken).ConfigureAwait(false);

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

            await settingsManager.SaveSettingAsync(settingKey, initialValue, ct: this.CancellationToken).ConfigureAwait(false);
            await settingsManager.SaveSettingAsync(settingKey, updatedValue, ct: this.CancellationToken).ConfigureAwait(false);
            var retrievedValue = await settingsManager.LoadSettingAsync(settingKey, ct: this.CancellationToken).ConfigureAwait(false);

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

            var retrievedValue = await settingsManager.LoadSettingAsync(new Settings.SettingKey<object?>(moduleName, key), ct: this.CancellationToken).ConfigureAwait(false);

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

            var retrievedValue = await settingsManager.LoadSettingOrDefaultAsync(new Settings.SettingKey<object?>(moduleName, key), defaultValue, ct: this.CancellationToken).ConfigureAwait(false);

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

            await settingsManager.SaveSettingAsync(new Settings.SettingKey<object>(moduleName, key), value, ct: this.CancellationToken).ConfigureAwait(false);
            var retrievedValue = await settingsManager.LoadSettingAsync(new Settings.SettingKey<object?>(moduleName, key), ct: this.CancellationToken).ConfigureAwait(false);

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
            settingsManager.SaveSettingAsync(new Settings.SettingKey<object?>(moduleName, key), value, ct: this.CancellationToken).Wait(this.CancellationToken);

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

            settingsManager.SaveSettingAsync(new Settings.SettingKey<object?>(moduleName, differentKey), value, ct: this.CancellationToken).Wait(this.CancellationToken);

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

            await settingsManager.SaveSettingAsync(new Settings.SettingKey<int>(moduleName, key), value, ct: this.CancellationToken).ConfigureAwait(false);
            settingsManager.ClearCache();
            var retrievedValue = await settingsManager.LoadSettingAsync(new Settings.SettingKey<int>(moduleName, key), ct: this.CancellationToken).ConfigureAwait(false);

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

            await settingsManager.SaveSettingAsync(new Settings.SettingKey<string?>(moduleName, key), value, ct: this.CancellationToken).ConfigureAwait(false);
            settingsManager.ClearCache();
            var retrievedValue = await settingsManager.LoadSettingAsync(new Settings.SettingKey<string?>(moduleName, key), ct: this.CancellationToken).ConfigureAwait(false);

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

            await settingsManager.SaveSettingAsync(new Settings.SettingKey<List<string>>(moduleName, key), value, ct: this.CancellationToken).ConfigureAwait(false);
            settingsManager.ClearCache();
            var retrievedValue = await settingsManager.LoadSettingAsync(new Settings.SettingKey<List<string>>(moduleName, key), ct: this.CancellationToken).ConfigureAwait(false);

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

            await settingsManager.SaveSettingAsync(new Settings.SettingKey<Dictionary<string, TestModel>>(moduleName, key), value, ct: this.CancellationToken).ConfigureAwait(false);
            settingsManager.ClearCache();
            var retrievedValue = await settingsManager.LoadSettingAsync(new Settings.SettingKey<Dictionary<string, TestModel>>(moduleName, key), ct: this.CancellationToken).ConfigureAwait(false);

            _ = retrievedValue.Should().BeEquivalentTo(value);
        }
    }

    [TestMethod]
    public async Task WhenSettingChanged_EventPayload_ContainsMetadata()
    {
        var scope = this.Container.OpenScope();
        await using (scope.ConfigureAwait(false))
        {
            var settingsManager = scope.Resolve<EditorSettingsManager>();

            var key = new Settings.SettingKey<string?>("TestModule", "NotifyKey");
            string? oldVal = null;
            string? newVal = null;
            SettingScope? gotScope = null;
            string? gotScopeId = null;

            var subject = settingsManager.WhenSettingChanged(key);
            using var sub = subject.Subscribe(evt =>
            {
                oldVal = evt.OldValue;
                newVal = evt.NewValue;
                gotScope = evt.Scope;
                gotScopeId = evt.ScopeId;
            });

            await settingsManager.SaveSettingAsync(key, "First", SettingContext.Project("projX"), ct: this.CancellationToken).ConfigureAwait(false);
            await settingsManager.SaveSettingAsync(key, "Second", SettingContext.Project("projX"), ct: this.CancellationToken).ConfigureAwait(false);

            _ = oldVal.Should().BeNull(); // SaveSettingAsync currently does not supply oldValue
            _ = newVal.Should().Be("Second");
            _ = gotScope.Should().Be(SettingScope.Project);
            _ = gotScopeId.Should().Be("projX");
        }
    }

    [TestMethod]
    public async Task SaveSettingAsync_DoesNotValidateDescriptorValidators()
    {
        var scope = this.Container.OpenScope();
        await using (scope.ConfigureAwait(false))
        {
            var settingsManager = scope.Resolve<EditorSettingsManager>();

            var descriptor = new Settings.SettingDescriptor<int>()
            {
                Key = new Settings.SettingKey<int>("ValidationModule", "MyInt"),
                Validators = [
                    new System.ComponentModel.DataAnnotations.RangeAttribute(1, 10)
                    {
                        ErrorMessage = "out-of-range",
                    },
                ],
            };
            EditorSettingsManager.StaticProvider.Register(descriptor);

            // Although descriptor invalidates values, SaveSettingAsync should not throw validation exception
            await settingsManager.SaveSettingAsync(descriptor.Key, 100, ct: this.CancellationToken).ConfigureAwait(false);

            var val = await settingsManager.LoadSettingAsync(new Settings.SettingKey<int>("ValidationModule", "MyInt"), ct: this.CancellationToken).ConfigureAwait(false);
            _ = val.Should().Be(100);
        }
    }

    [TestMethod]
    public async Task SaveSettingAsync_NullValue_PersistsRecord_BatchDelete_RemovesRecord()
    {
        var scope = this.Container.OpenScope();
        await using (scope.ConfigureAwait(false))
        {
            var settingsManager = scope.Resolve<EditorSettingsManager>();
            var ctx = scope.Resolve<PersistentState>();

            var key = new Settings.SettingKey<string?>("BatchModule", "DeleteVsNull");
            await settingsManager.SaveSettingAsync(key, "Val", ct: this.CancellationToken).ConfigureAwait(false);

            // Save null via SaveSettingAsync -> should persist a record with JSON "null"
            await settingsManager.SaveSettingAsync(key, (string?)null, ct: this.CancellationToken).ConfigureAwait(false);
            var recordAfterNull = ctx.Settings.SingleOrDefault(ms => ms.SettingsModule == key.SettingsModule && ms.Name == key.Name);
            _ = recordAfterNull.Should().NotBeNull();

            // Now delete via batch
            var batch = settingsManager.BeginBatch();
            await using (batch.ConfigureAwait(false))
            {
                _ = batch.QueuePropertyChange(new Settings.SettingDescriptor<string?>() { Key = key }, value: null);
            }

            var recordAfterDelete = ctx.Settings.SingleOrDefault(ms => ms.SettingsModule == key.SettingsModule && ms.Name == key.Name);
            _ = recordAfterDelete.Should().BeNull();
        }
    }

    [TestMethod]
    public async Task GetLastUpdatedTimeAsync_ShouldReturnUpdatedAtAfterSave()
    {
        var scope = this.Container.OpenScope();
        await using (scope.ConfigureAwait(false))
        {
            var settingsManager = scope.Resolve<EditorSettingsManager>();

            const string moduleName = "MetaModule";
            const string key = "MetaKey";
            var settingKey = new Settings.SettingKey<object?>(moduleName, key);
            var value = new { Setting = "Value" };

            var beforeSave = DateTime.UtcNow;
            await settingsManager.SaveSettingAsync(settingKey, value, ct: this.CancellationToken).ConfigureAwait(false);
            var lastUpdated = await settingsManager.GetLastUpdatedTimeAsync(settingKey, ct: this.CancellationToken).ConfigureAwait(false);

            _ = lastUpdated.Should().NotBeNull();
            _ = lastUpdated.Value.Should().BeAfter(beforeSave.AddSeconds(-1));
            _ = lastUpdated.Value.Should().BeBefore(DateTime.UtcNow.AddSeconds(1));
        }
    }

    [TestMethod]
    public async Task GetLastUpdatedTimeAsync_ShouldUpdateOnSubsequentSave()
    {
        var scope = this.Container.OpenScope();
        await using (scope.ConfigureAwait(false))
        {
            var settingsManager = scope.Resolve<EditorSettingsManager>();

            const string moduleName = "MetaModule2";
            const string key = "MetaKey2";
            var settingKey = new Settings.SettingKey<int>(moduleName, key);

            await settingsManager.SaveSettingAsync(settingKey, 1, ct: this.CancellationToken).ConfigureAwait(false);
            var t1 = await settingsManager.GetLastUpdatedTimeAsync(settingKey, ct: this.CancellationToken).ConfigureAwait(false);

            // Ensure a time difference
            await Task.Delay(50, this.CancellationToken).ConfigureAwait(false);

            await settingsManager.SaveSettingAsync(settingKey, 2, ct: this.CancellationToken).ConfigureAwait(false);
            var t2 = await settingsManager.GetLastUpdatedTimeAsync(settingKey, ct: this.CancellationToken).ConfigureAwait(false);

            _ = t1.Should().NotBeNull();
            _ = t2.Should().NotBeNull();
            _ = t2.Value.Should().BeAfter(t1.Value);
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
            var descriptorsByCategory = settingsManager.GetDescriptorsByCategory();

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
            var matches = settingsManager.SearchDescriptors("Window");

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
