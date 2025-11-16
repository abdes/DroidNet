// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.ComponentModel;
using System.ComponentModel.DataAnnotations;
using System.Diagnostics.CodeAnalysis;
using System.Text.Json;
using AwesomeAssertions;
using DryIoc;
using Oxygen.Editor.Data.Settings;

namespace Oxygen.Editor.Data.Tests;

[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory("Settings Discovery")]
public class DiscoveryApisTests : DatabaseTests
{
    public DiscoveryApisTests()
    {
        // Use composite provider to include reflection-based discovery for test descriptors
        this.Container.RegisterDelegate<IDescriptorProvider>(
            _ => new CompositeDescriptorProvider(
                EditorSettingsManager.StaticProvider,
                new ReflectionDescriptorProvider()),
            Reuse.Singleton);
        this.Container.Register<EditorSettingsManager>(Reuse.Scoped);
    }

    public TestContext TestContext { get; set; }

    [TestMethod]
    public async Task GetDescriptorsByCategoryAsync_ShouldReturnGroupedDescriptors()
    {
        var scope = this.Container.OpenScope();
        await using (scope.ConfigureAwait(false))
        {
            var mgr = scope.Resolve<EditorSettingsManager>();

            var grouped = await mgr.GetDescriptorsByCategoryAsync(this.TestContext.CancellationToken).ConfigureAwait(false);

            _ = grouped.Should().NotBeNull();
            _ = grouped.Should().ContainKey("General");
            _ = grouped.Should().ContainKey("Visual");

            var general = grouped["General"];
            _ = general.Select(d => d.Name).Should().Contain(["Hello", "Count"]);

            var visual = grouped["Visual"];
            _ = visual.Select(d => d.Name).Should().Contain("Opacity");
        }
    }

    [TestMethod]
    public async Task SearchDescriptorsAsync_ShouldFindDescriptorByNameOrDisplay()
    {
        var scope = this.Container.OpenScope();
        await using (scope.ConfigureAwait(false))
        {
            var mgr = scope.Resolve<EditorSettingsManager>();

            var res1 = await mgr.SearchDescriptorsAsync("Hello", this.TestContext.CancellationToken).ConfigureAwait(false);
            _ = res1.Select(d => d.Name).Should().Contain("Hello");

            var res2 = await mgr.SearchDescriptorsAsync("Count Display", this.TestContext.CancellationToken).ConfigureAwait(false);
            _ = res2.Select(d => d.Name).Should().Contain("Count");

            var res3 = await mgr.SearchDescriptorsAsync("Opacity", this.TestContext.CancellationToken).ConfigureAwait(false);
            _ = res3.Select(d => d.Name).Should().Contain("Opacity");
        }
    }

    [TestMethod]
    public async Task GetAllKeysAsync_ShouldReturnSavedKeys()
    {
        var scope = this.Container.OpenScope();
        await using (scope.ConfigureAwait(false))
        {
            var mgr = scope.Resolve<EditorSettingsManager>();

            await mgr.SaveSettingAsync(new Settings.SettingKey<string>("Discovery", "Key1"), "Val1", ct: this.TestContext.CancellationToken).ConfigureAwait(false);
            await mgr.SaveSettingAsync(new Settings.SettingKey<string>("Discovery", "Key2"), "Val2", ct: this.TestContext.CancellationToken).ConfigureAwait(false);

            var keys = await mgr.GetAllKeysAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
            _ = keys.Should().Contain(["Discovery/Key1", "Discovery/Key2"]);
        }
    }

    [TestMethod]
    public async Task GetAllValuesAsync_ShouldReturnAllScopesAndValues()
    {
        var scope = this.Container.OpenScope();
        await using (scope.ConfigureAwait(false))
        {
            var mgr = scope.Resolve<EditorSettingsManager>();
            var key = new Settings.SettingKey<string>("Discovery", "ScopedKey");

            await mgr.SaveSettingAsync(key, "appVal", SettingContext.Application(), this.TestContext.CancellationToken).ConfigureAwait(false);
            await mgr.SaveSettingAsync(key, "projVal", SettingContext.Project("proj1"), this.TestContext.CancellationToken).ConfigureAwait(false);

            var all = await mgr.GetAllValuesAsync("Discovery/ScopedKey", this.TestContext.CancellationToken).ConfigureAwait(false);
            _ = all.Should().HaveCount(2);

            var app = all.First(i => i.scope == SettingScope.Application);
            var appValue = app.value is JsonElement appJe ? appJe.GetString() : app.value?.ToString();
            _ = appValue.Should().Be("appVal");

            var proj = all.First(i => i.scope == SettingScope.Project && string.Equals(i.scopeId, "proj1", StringComparison.Ordinal));
            var projValue = proj.value is JsonElement projJe ? projJe.GetString() : proj.value?.ToString();
            _ = projValue.Should().Be("projVal");
        }
    }

    [TestMethod]
    public async Task GetAllValuesAsync_TypedGeneric_ShouldReturnTypedValues()
    {
        var scope = this.Container.OpenScope();
        await using (scope.ConfigureAwait(false))
        {
            var mgr = scope.Resolve<EditorSettingsManager>();
            var key = new Settings.SettingKey<string>("Discovery", "TypedKey");

            await mgr.SaveSettingAsync(key, "appVal", SettingContext.Application(), this.TestContext.CancellationToken).ConfigureAwait(false);
            await mgr.SaveSettingAsync(key, "projVal", SettingContext.Project("proj1"), this.TestContext.CancellationToken).ConfigureAwait(false);

            var all = await mgr.GetAllValuesAsync<string>("Discovery/TypedKey", this.TestContext.CancellationToken).ConfigureAwait(false);
            _ = all.Should().HaveCount(2);

            var app = all.First(i => i.scope == SettingScope.Application);
            _ = app.value.Should().Be("appVal");

            var proj = all.First(i => i.scope == SettingScope.Project && string.Equals(i.scopeId, "proj1", StringComparison.Ordinal));
            _ = proj.value.Should().Be("projVal");
        }
    }

    [TestMethod]
    public async Task TryGetAllValuesAsync_ShouldReturnErrorsForMismatchedTypes()
    {
        var scope = this.Container.OpenScope();
        await using (scope.ConfigureAwait(false))
        {
            var mgr = scope.Resolve<EditorSettingsManager>();
            var key = new Settings.SettingKey<string>("Discovery", "MismatchedKey");

            await mgr.SaveSettingAsync(key, "not-a-number", SettingContext.Application(), this.TestContext.CancellationToken).ConfigureAwait(false);

            var result = await mgr.TryGetAllValuesAsync<int>("Discovery/MismatchedKey", this.TestContext.CancellationToken).ConfigureAwait(false);
            _ = result.Success.Should().BeFalse();
            _ = result.Errors.Should().NotBeEmpty();
            _ = result.Values.Should().ContainSingle();
            _ = result.Values[0].Value.Should().Be(default);
        }
    }

    [SuppressMessage("Performance", "CA1812:Avoid uninstantiated internal classes", Justification = "Used via runtime reflection for settings descriptor discovery in tests.")]
    private sealed class TestDescriptors : SettingsDescriptorSet
    {
        [Display(Name = "Hello Display", Description = "A hello descriptor")]
        [Category("General")]
        public static SettingDescriptor<string> Hello { get; } = CreateDescriptor<string>("Discovery", "Hello");

        [Display(Name = "Count Display")]
        [Category("General")]
        public static SettingDescriptor<int> Count { get; } = CreateDescriptor<int>("Discovery", "Count");

        [Display(Name = "Opacity")]
        [Category("Visual")]
        public static SettingDescriptor<double> Opacity { get; } = CreateDescriptor<double>("Discovery", "Opacity");
    }
}
