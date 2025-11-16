// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.ComponentModel;
using System.ComponentModel.DataAnnotations;
using System.Diagnostics.CodeAnalysis;
using AwesomeAssertions;
using DryIoc;
using Oxygen.Editor.Data.Settings;

namespace Oxygen.Editor.Data.Tests;

[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory("Settings UI Discovery")]
public class DiscoveryUiGenerationTests : DatabaseTests
{
    public DiscoveryUiGenerationTests()
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
    public async Task Descriptors_ShouldMapToUIControlTypes()
    {
        var scope = this.Container.OpenScope();
        await using (scope.ConfigureAwait(false))
        {
            var mgr = scope.Resolve<EditorSettingsManager>();
            var grouped = await mgr.GetDescriptorsByCategoryAsync(this.TestContext.CancellationToken).ConfigureAwait(false);

            _ = grouped.Should().ContainKey("General");
            _ = grouped.Should().ContainKey("Visual");

            // Helper function to deduce a simple control type from the descriptor's value type.
            static string DeduceControlType(ISettingDescriptor d)
            {
                var descriptorType = d.GetType();
                if (descriptorType.IsGenericType)
                {
                    var valueType = descriptorType.GetGenericArguments()[0];
                    if (valueType == typeof(string))
                    {
                        return "TextBox";
                    }

                    if (valueType == typeof(bool))
                    {
                        return "CheckBox";
                    }

                    if (valueType == typeof(int) || valueType == typeof(double) || valueType == typeof(float) || valueType == typeof(long))
                    {
                        return "NumericBox";
                    }

                    // Fallback
                    return "TextBox";
                }

                return "TextBox";
            }

            var general = grouped["General"];
            var hello = general.First(d => string.Equals(d.Name, "Hello", System.StringComparison.Ordinal));
            var count = general.First(d => string.Equals(d.Name, "Count", System.StringComparison.Ordinal));

            _ = DeduceControlType(hello).Should().Be("TextBox");
            _ = DeduceControlType(count).Should().Be("NumericBox");

            var visual = grouped["Visual"];
            var opacity = visual.First(d => string.Equals(d.Name, "Opacity", System.StringComparison.Ordinal));
            _ = DeduceControlType(opacity).Should().Be("NumericBox");
        }
    }

    [TestMethod]
    public async Task SearchDescriptorsAsync_ShouldProvideUIFriendlySearchResults()
    {
        var scope = this.Container.OpenScope();
        await using (scope.ConfigureAwait(false))
        {
            var mgr = scope.Resolve<EditorSettingsManager>();

            var results = await mgr.SearchDescriptorsAsync("Hello", this.TestContext.CancellationToken).ConfigureAwait(false);
            _ = results.Select(d => d.Name).Should().Contain("Hello");
            var r = results.First(d => string.Equals(d.Name, "Hello", System.StringComparison.Ordinal));

            // Build a simple UI search model
            var searchVM = new
            {
                Key = r.SettingsModule + "/" + r.Name,
                Display = r.DisplayName ?? r.Name,
                r.Description,
                r.Category,
            };

            _ = searchVM.Display.Should().Be("Hello Display");
            _ = searchVM.Category.Should().Be("General");
            _ = searchVM.Key.Should().Be("Discovery/Hello");
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
