// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using AwesomeAssertions;
using DryIoc;
using Oxygen.Editor.Data.Models;
using Oxygen.Editor.Data.Services;
using Oxygen.Editor.Data.Settings;

namespace Oxygen.Editor.Data.Tests;

[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory("Module Settings")]
public class ModuleSettingsTests : DatabaseTests
{
    public ModuleSettingsTests()
    {
        // Use composite provider to include reflection-based discovery for test descriptors
        this.Container.RegisterDelegate<IDescriptorProvider>(
            _ => new CompositeDescriptorProvider(
                EditorSettingsManager.StaticProvider,
                new ReflectionDescriptorProvider()),
            Reuse.Singleton);
        this.Container.Register<EditorSettingsManager>(Reuse.Scoped);
    }

    [TestMethod]
    public async Task SaveAsync_ShouldSaveModifiedProperties()
    {
        var scope = this.Container.OpenScope();
        await using (scope.ConfigureAwait(false))
        {
            var settingsManager = scope.Resolve<EditorSettingsManager>();
            var descriptors = settingsManager.GetDescriptorsByCategory();
            var ds = descriptors.Values.SelectMany(v => v);
            _ = ds.Should().Contain(d => d.SettingsModule == "TestModule" && d.Name == nameof(TestModuleSettings.TestProperty));
            var moduleSettings = new TestModuleSettings("TestModule")
            {
                TestProperty = "NewValue",
            };
            await moduleSettings.SaveAsync(settingsManager, this.CancellationToken).ConfigureAwait(false);

            var retrievedValue = await settingsManager.LoadSettingAsync(
                new SettingKey<string?>("TestModule", nameof(moduleSettings.TestProperty)),
                ct: this.CancellationToken).ConfigureAwait(false);
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
            var descriptors = settingsManager.GetDescriptorsByCategory();
            var ds = descriptors.Values.SelectMany(v => v);
            _ = ds.Should().Contain(d => d.SettingsModule == "TestModule" && d.Name == nameof(TestModuleSettings.TestProperty));
            var moduleSettings = new TestModuleSettings("TestModule");

            await settingsManager.SaveSettingAsync(
                new SettingKey<string?>("TestModule", nameof(moduleSettings.TestProperty)),
                "LoadedValue",
                ct: this.CancellationToken).ConfigureAwait(false);
            await moduleSettings.LoadAsync(settingsManager, ct: this.CancellationToken).ConfigureAwait(false);

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
            var moduleSettings = new TestModuleSettings("TestModule");

            await moduleSettings.SaveAsync(settingsManager, ct: this.CancellationToken).ConfigureAwait(false);

            var retrievedValue = await settingsManager.LoadSettingAsync(
                new SettingKey<string?>("TestModule", nameof(moduleSettings.TestProperty)),
                ct: this.CancellationToken).ConfigureAwait(false);
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
            var moduleSettings = new TestModuleSettings("TestModule")
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
            var moduleSettings = new TestModuleSettings("TestModule");

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
            var moduleSettings = new TestModuleSettings("TestModule");

            await moduleSettings.LoadAsync(settingsManager, ct: this.CancellationToken).ConfigureAwait(false);

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
            var moduleSettings = new TestModuleSettings("TestModule")
            {
                TestProperty = null,
            };
            await moduleSettings.SaveAsync(settingsManager, ct: this.CancellationToken).ConfigureAwait(false);

            var retrievedValue = await settingsManager.LoadSettingAsync(
                new SettingKey<string?>("TestModule", nameof(moduleSettings.TestProperty)),
                ct: this.CancellationToken).ConfigureAwait(false);
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
            var moduleSettings = new TestModuleSettings("TestModule");

            await settingsManager.SaveSettingAsync(
                new SettingKey<string?>("TestModule", nameof(moduleSettings.TestProperty)),
                "LoadedValue",
                ct: this.CancellationToken).ConfigureAwait(false);
            await moduleSettings.LoadAsync(settingsManager, ct: this.CancellationToken).ConfigureAwait(false);

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
            var descriptors = settingsManager.GetDescriptorsByCategory();
            var ds = descriptors.Values.SelectMany(v => v);
            _ = ds.Should().Contain(d => d.SettingsModule == "TestModule" && d.Name == nameof(FailingTestModuleSettings.PrivateGetterProperty));
            var moduleSettings = new FailingTestModuleSettings("TestModule");

            Func<Task> act = () => moduleSettings.LoadAsync(settingsManager, ct: this.CancellationToken);

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
            var moduleSettings = new TestModuleSettings("TestModule")
            {
                TestProperty = "NewValue",
            };

            await moduleSettings.SaveAsync(settingsManager, ct: this.CancellationToken).ConfigureAwait(false);

            _ = moduleSettings.OnSavingCalled.Should().BeTrue();
        }
    }

    [TestMethod]
    public async Task GetLastUpdatedTimeAsync_PropertyExtension_ReturnsLastUpdatedTime()
    {
        var scope = this.Container.OpenScope();
        await using (scope.ConfigureAwait(false))
        {
            var settingsManager = scope.Resolve<EditorSettingsManager>();
            var moduleSettings = new TestModuleSettings("TestModule")
            {
                TestProperty = "NewValue",
            };

            await moduleSettings.SaveAsync(settingsManager, ct: this.CancellationToken).ConfigureAwait(false);
            var lastUpdated = await moduleSettings.GetLastUpdatedTimeAsync(nameof(moduleSettings.TestProperty), settingsManager, ct: this.CancellationToken).ConfigureAwait(false);

            _ = lastUpdated.Should().NotBeNull();
            _ = lastUpdated.Value.Should().BeBefore(DateTime.UtcNow.AddSeconds(1));
        }
    }

    [TestMethod]
    public async Task SaveAsync_ShouldThrowWhenPropertyAccessThrows()
    {
        var scope = this.Container.OpenScope();
        await using (scope.ConfigureAwait(false))
        {
            var settingsManager = scope.Resolve<EditorSettingsManager>();
            var descriptors = settingsManager.GetDescriptorsByCategory();
            var ds = descriptors.Values.SelectMany(v => v);
            _ = ds.Should().Contain(d => d.SettingsModule == "TestModule" && d.Name == nameof(FailingTestModuleSettings.PrivateGetterProperty));
            var moduleSettings = new FailingTestModuleSettings("TestModule")
            {
                PrivateGetterProperty = "NewValue",
            };

            Func<Task> act = () => moduleSettings.SaveAsync(settingsManager, ct: this.CancellationToken);

            _ = await act.Should().ThrowAsync<InvalidOperationException>().ConfigureAwait(false);
        }
    }

    // Descriptor set discovered via reflection by the EditorSettingsManager to enable
    // descriptor-driven persistence in tests.
    [SuppressMessage("Performance", "CA1812:Avoid uninstantiated internal classes", Justification = "Used via runtime reflection for settings descriptor discovery in tests.")]
    private sealed class TestModuleSettingsDescriptors : SettingsDescriptorSet
    {
        public static SettingDescriptor<string?> TestProperty { get; } = CreateDescriptor<string?>("TestModule", nameof(TestModuleSettings.TestProperty));

        public static SettingDescriptor<string?> PrivateGetterProperty { get; } = CreateDescriptor<string?>("TestModule", nameof(FailingTestModuleSettings.PrivateGetterProperty));
    }

    private sealed class TestModuleSettings(string moduleName)
        : ModuleSettings(moduleName)
    {
        private string? testProperty;

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

    private sealed class FailingTestModuleSettings(string moduleName)
        : ModuleSettings(moduleName)
    {
        private string testProperty = "PrivateValue";

        [SuppressMessage("ReSharper", "UnusedMember.Local", Justification = "needed for testing")]
        public static string ReadOnlyProperty => "ReadOnlyValue";

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
