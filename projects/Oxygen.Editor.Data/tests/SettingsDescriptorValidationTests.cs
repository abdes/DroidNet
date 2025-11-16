// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.ComponentModel.DataAnnotations;
using System.Diagnostics.CodeAnalysis;
using AwesomeAssertions;
using DryIoc;
using Oxygen.Editor.Data.Settings;

namespace Oxygen.Editor.Data.Tests;

// Test code will always want the default behavior of ConfigureAwait(), and we don't want to
// double the number of lines of code when using `await using`.
#pragma warning disable CA2007 // Consider calling ConfigureAwait on the awaited task

[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory("Settings Descriptor")]
public class SettingsDescriptorValidationTests : DatabaseTests
{
    public SettingsDescriptorValidationTests()
    {
        this.Container.Register<EditorSettingsManager>(Reuse.Scoped);
    }

    public TestContext TestContext { get; set; }

    [TestMethod]
    public async Task SaveSettingAsync_WithRequiredValidator_ShouldRejectNull()
    {
        var scope = this.Container.OpenScope();
        await using (scope.ConfigureAwait(false))
        {
            var manager = scope.Resolve<EditorSettingsManager>();

            var descriptor = new SettingDescriptor<string>
            {
                Key = new SettingKey<string>("TestModule", "RequiredKey"),
                Validators = [new RequiredAttribute()],
            };

            var act = () => manager.SaveSettingAsync(descriptor, null!, SettingContext.Application(), this.TestContext.CancellationToken);
            _ = await act.Should().ThrowAsync<SettingsValidationException>().ConfigureAwait(false);
        }
    }

    [TestMethod]
    public async Task SaveSettingAsync_WithRangeValidator_ShouldRejectOutOfRange()
    {
        var scope = this.Container.OpenScope();
        await using (scope.ConfigureAwait(false))
        {
            var manager = scope.Resolve<EditorSettingsManager>();

            var descriptor = new SettingDescriptor<int>
            {
                Key = new SettingKey<int>("TestModule", "RangeKey"),
                Validators = [new RangeAttribute(1, 10)],
            };

            var act = () => manager.SaveSettingAsync(descriptor, 0, SettingContext.Application(), this.TestContext.CancellationToken);
            _ = await act.Should().ThrowAsync<SettingsValidationException>().ConfigureAwait(false);

            // Valid value should save
            await manager.SaveSettingAsync(descriptor, 5, SettingContext.Application()).ConfigureAwait(false);
            var loaded = await manager.LoadSettingAsync(new SettingKey<int>("TestModule", "RangeKey"), ct: this.TestContext.CancellationToken).ConfigureAwait(false);
            _ = loaded.Should().Be(5);
        }
    }

    [TestMethod]
    public async Task SaveSettingAsync_WithMultipleValidators_ReturnsAllValidationResults()
    {
        var scope = this.Container.OpenScope();
        await using (scope.ConfigureAwait(false))
        {
            var manager = scope.Resolve<EditorSettingsManager>();

            var descriptor = new SettingDescriptor<int>
            {
                Key = new SettingKey<int>("TestModule", "MultiKey"),
                Validators = [
                    new RangeAttribute(1, 5) { ErrorMessage = "Range" },
                    new RangeAttribute(10, 20) { ErrorMessage = "Range2" },
                ],
            };

            var act = () => manager.SaveSettingAsync(descriptor, 0, SettingContext.Application());
            var ex = await act.Should()
                .ThrowAsync<SettingsValidationException>()
                .Where(ex => ex.Results.Count == 2)
                .ConfigureAwait(false);
        }
    }
}
