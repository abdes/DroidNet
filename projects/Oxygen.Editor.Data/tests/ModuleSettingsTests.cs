// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using AwesomeAssertions;
using DryIoc;
using Oxygen.Editor.Data.Models;

namespace Oxygen.Editor.Data.Tests;

[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory("Module Settings")]
public class ModuleSettingsTests : DatabaseTests
{
    public ModuleSettingsTests()
    {
        this.Container.Register<EditorSettingsManager>(Reuse.Scoped);
    }

    [TestMethod]
    public async Task SaveAsync_ShouldSaveModifiedProperties()
    {
        var scope = this.Container.OpenScope();
        await using (scope.ConfigureAwait(false))
        {
            var settingsManager = scope.Resolve<EditorSettingsManager>();
            var moduleSettings = new TestModuleSettings(settingsManager, "TestModule")
            {
                TestProperty = "NewValue",
            };
            await moduleSettings.SaveAsync().ConfigureAwait(false);

            var retrievedValue = await settingsManager.LoadSettingAsync<string>("TestModule", nameof(moduleSettings.TestProperty)).ConfigureAwait(false);
            _ = retrievedValue.Should().Be("NewValue");
        }
    }

    [TestMethod]
    public async Task LoadAsync_ShouldLoadProperties()
    {
        var scope = this.Container.OpenScope();
        await using (scope.ConfigureAwait(false))
        {
            var settingsManager = scope.Resolve<EditorSettingsManager>();
            var moduleSettings = new TestModuleSettings(settingsManager, "TestModule");

            await settingsManager.SaveSettingAsync("TestModule", nameof(moduleSettings.TestProperty), "LoadedValue").ConfigureAwait(false);
            await moduleSettings.LoadAsync().ConfigureAwait(false);

            _ = moduleSettings.TestProperty.Should().Be("LoadedValue");
            _ = moduleSettings.IsLoaded.Should().BeTrue();
            _ = moduleSettings.IsDirty.Should().BeFalse();
        }
    }

    [TestMethod]
    public async Task SaveAsync_ShouldNotSaveUnmodifiedProperties()
    {
        var scope = this.Container.OpenScope();
        await using (scope.ConfigureAwait(false))
        {
            var settingsManager = scope.Resolve<EditorSettingsManager>();
            var moduleSettings = new TestModuleSettings(settingsManager, "TestModule");

            await moduleSettings.SaveAsync().ConfigureAwait(false);

            var retrievedValue = await settingsManager.LoadSettingAsync<string>("TestModule", nameof(moduleSettings.TestProperty)).ConfigureAwait(false);
            _ = retrievedValue.Should().BeNull();
        }
    }

    [TestMethod]
    public async Task SetProperty_ShouldMarkAsDirty()
    {
        var scope = this.Container.OpenScope();
        await using (scope.ConfigureAwait(false))
        {
            var settingsManager = scope.Resolve<EditorSettingsManager>();
            var moduleSettings = new TestModuleSettings(settingsManager, "TestModule")
            {
                TestProperty = "NewValue",
            };

            _ = moduleSettings.IsDirty.Should().BeTrue();
        }
    }

    [TestMethod]
    public async Task SetProperty_ShouldRaisePropertyChangedEvent()
    {
        var scope = this.Container.OpenScope();
        await using (scope.ConfigureAwait(false))
        {
            var settingsManager = scope.Resolve<EditorSettingsManager>();
            var moduleSettings = new TestModuleSettings(settingsManager, "TestModule");

            var propertyChangedRaised = false;
            moduleSettings.PropertyChanged += (_, args) =>
            {
                if (string.Equals(args.PropertyName, nameof(moduleSettings.TestProperty), StringComparison.Ordinal))
                {
                    propertyChangedRaised = true;
                }
            };

            moduleSettings.TestProperty = "NewValue";

            _ = propertyChangedRaised.Should().BeTrue();
        }
    }

    [TestMethod]
    public async Task LoadAsync_ShouldHandleDefaultValues()
    {
        var scope = this.Container.OpenScope();
        await using (scope.ConfigureAwait(false))
        {
            var settingsManager = scope.Resolve<EditorSettingsManager>();
            var moduleSettings = new TestModuleSettings(settingsManager, "TestModule");

            await moduleSettings.LoadAsync().ConfigureAwait(false);

            _ = moduleSettings.TestProperty.Should().BeNull();
            _ = moduleSettings.IsLoaded.Should().BeTrue();
            _ = moduleSettings.IsDirty.Should().BeFalse();
        }
    }

    [TestMethod]
    public async Task SaveAsync_ShouldHandleNullValues()
    {
        var scope = this.Container.OpenScope();
        await using (scope.ConfigureAwait(false))
        {
            var settingsManager = scope.Resolve<EditorSettingsManager>();
            var moduleSettings = new TestModuleSettings(settingsManager, "TestModule")
            {
                TestProperty = null,
            };
            await moduleSettings.SaveAsync().ConfigureAwait(false);

            var retrievedValue = await settingsManager.LoadSettingAsync<string>("TestModule", nameof(moduleSettings.TestProperty)).ConfigureAwait(false);
            _ = retrievedValue.Should().BeNull();
        }
    }

    [TestMethod]
    public async Task LoadAsync_ShouldCallOnLoadedWhenSuccessful()
    {
        var scope = this.Container.OpenScope();
        await using (scope.ConfigureAwait(false))
        {
            var settingsManager = scope.Resolve<EditorSettingsManager>();
            var moduleSettings = new TestModuleSettings(settingsManager, "TestModule");

            await settingsManager.SaveSettingAsync("TestModule", nameof(moduleSettings.TestProperty), "LoadedValue").ConfigureAwait(false);
            await moduleSettings.LoadAsync().ConfigureAwait(false);

            _ = moduleSettings.OnLoadedCalled.Should().BeTrue();
        }
    }

    [TestMethod]
    public async Task LoadAsync_ShouldNotCallOnLoadedWhenNotSuccessful()
    {
        var scope = this.Container.OpenScope();
        await using (scope.ConfigureAwait(false))
        {
            var settingsManager = scope.Resolve<EditorSettingsManager>();
            var moduleSettings = new FailingTestModuleSettings(settingsManager, "TestModule");

            var act = moduleSettings.LoadAsync;

            _ = await act.Should().ThrowAsync<InvalidOperationException>().ConfigureAwait(false);
            _ = moduleSettings.OnLoadedCalled.Should().BeFalse();
        }
    }

    [TestMethod]
    public async Task SaveAsync_ShouldCallOnSavingBeforeSave()
    {
        var scope = this.Container.OpenScope();
        await using (scope.ConfigureAwait(false))
        {
            var settingsManager = scope.Resolve<EditorSettingsManager>();
            var moduleSettings = new TestModuleSettings(settingsManager, "TestModule")
            {
                TestProperty = "NewValue",
            };

            await moduleSettings.SaveAsync().ConfigureAwait(false);

            _ = moduleSettings.OnSavingCalled.Should().BeTrue();
        }
    }

    [TestMethod]
    public async Task SaveAsync_ShouldThrowWhenPropertyAccessThrows()
    {
        var scope = this.Container.OpenScope();
        await using (scope.ConfigureAwait(false))
        {
            var settingsManager = scope.Resolve<EditorSettingsManager>();
            var moduleSettings = new FailingTestModuleSettings(settingsManager, "TestModule")
            {
                PrivateGetterProperty = "NewValue",
            };

            var act = moduleSettings.SaveAsync;

            _ = await act.Should().ThrowAsync<InvalidOperationException>().ConfigureAwait(false);
        }
    }

    private sealed class TestModuleSettings(EditorSettingsManager settingsManager, string moduleName)
        : ModuleSettings(settingsManager, moduleName)
    {
        private string? testProperty;

        [Persisted]
        public string? TestProperty
        {
            get => this.testProperty;
            set => this.SetProperty(ref this.testProperty, value);
        }

        public bool OnLoadedCalled { get; private set; }

        public bool OnSavingCalled { get; private set; }

        protected override void OnSaving()
        {
            base.OnSaving();
            this.OnSavingCalled = true;
        }

        protected override void OnLoaded()
        {
            this.OnLoadedCalled = true;
            base.OnLoaded();
        }
    }

    private sealed class FailingTestModuleSettings(EditorSettingsManager settingsManager, string moduleName)
        : ModuleSettings(settingsManager, moduleName)
    {
        private string testProperty = "PrivateValue";

        [Persisted]
        [SuppressMessage("ReSharper", "UnusedMember.Local", Justification = "needed for testing")]
        public static string ReadOnlyProperty => "ReadOnlyValue";

        [Persisted]
        [SuppressMessage("ReSharper", "UnusedMember.Local", Justification = "needed for testing")]
        public string PrivateGetterProperty
        {
            get => throw new InvalidOperationException("exception created on purpose for testing");
            set => this.SetProperty(ref this.testProperty, value);
        }

        public bool OnLoadedCalled { get; private set; }

        protected override void OnLoaded() => this.OnLoadedCalled = true;
    }
}
