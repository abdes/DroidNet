// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.ComponentModel.DataAnnotations;
using System.Diagnostics.CodeAnalysis;
using System.Drawing;
using AwesomeAssertions;
using DryIoc;
using Oxygen.Editor.Data.Models;
using Oxygen.Editor.Data.Settings;

namespace Oxygen.Editor.Data.Tests;

[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory("Example Settings")]
public class ExampleSettingsTests : DatabaseTests
{
    /// <summary>
    /// Initializes a new instance of the <see cref="ExampleSettingsTests"/> class.
    /// </summary>
    public ExampleSettingsTests()
    {
        this.Container.Register<EditorSettingsManager>(Reuse.Scoped);
    }

    [TestMethod]
    public async Task WindowPositionShouldHaveDescriptor()
    {
        var scope = this.Container.OpenScope();
        await using (scope.ConfigureAwait(false))
        {
            var mgr = scope.Resolve<EditorSettingsManager>();
            var grouped = mgr.GetDescriptorsByCategory();
            _ = grouped.Should().ContainKey("Layout");
            var layout = grouped["Layout"];
            _ = layout.Select(d => d.Name).Should().Contain("WindowPosition");
        }
    }

    [TestMethod]
    public async Task WindowSizeShouldHaveDescriptor()
    {
        var scope = this.Container.OpenScope();
        await using (scope.ConfigureAwait(false))
        {
            var mgr = scope.Resolve<EditorSettingsManager>();
            var grouped = mgr.GetDescriptorsByCategory();
            _ = grouped.Should().ContainKey("Layout");
            var layout = grouped["Layout"];
            _ = layout.Select(d => d.Name).Should().Contain("WindowSize");
        }
    }

    [TestMethod]
    public void WindowPositionShouldValidateNonNegativeCoordinates()
    {
        var scope = this.Container.OpenScope();
        using (scope)
        {
            var settingsManager = scope.Resolve<EditorSettingsManager>();
            var moduleSettings = new ExampleSettings();

            Action act = () => moduleSettings.WindowPosition = new Point(-1, -1);

            _ = act.Should().Throw<ValidationException>().WithMessage("X and Y coordinates must be non-negative.");
        }
    }

    [TestMethod]
    public void WindowSizeShouldValidatePositiveDimensions()
    {
        var scope = this.Container.OpenScope();
        using (scope)
        {
            var settingsManager = scope.Resolve<EditorSettingsManager>();
            var moduleSettings = new ExampleSettings();

            Action act = () => moduleSettings.WindowSize = new Size(0, 0);

            _ = act.Should().Throw<ValidationException>().WithMessage("Width and Height must be positive.");
        }
    }

    [TestMethod]
    public async Task SaveAsync_ShouldSaveWindowProperties()
    {
        var scope = this.Container.OpenScope();
        await using (scope.ConfigureAwait(false))
        {
            var mgr = scope.Resolve<EditorSettingsManager>();
            var moduleSettings = new ExampleSettings
            {
                WindowPosition = new Point(123, 456),
                WindowSize = new Size(800, 600),
            };

            await moduleSettings.SaveAsync(mgr, ct: this.CancellationToken).ConfigureAwait(false);

            var savedPos = await mgr.LoadSettingAsync(new SettingKey<Point>("Oxygen.Editor.Data.Example", nameof(moduleSettings.WindowPosition)), ct: this.CancellationToken).ConfigureAwait(false);
            var savedSize = await mgr.LoadSettingAsync(new SettingKey<Size>("Oxygen.Editor.Data.Example", nameof(moduleSettings.WindowSize)), ct: this.CancellationToken).ConfigureAwait(false);

            _ = savedPos.Should().Be(new Point(123, 456));
            _ = savedSize.Should().Be(new Size(800, 600));
        }
    }

    [TestMethod]
    public async Task LoadAsync_ShouldLoadWindowProperties()
    {
        var scope = this.Container.OpenScope();
        await using (scope.ConfigureAwait(false))
        {
            var mgr = scope.Resolve<EditorSettingsManager>();
            var moduleSettings = new ExampleSettings();

            await mgr.SaveSettingAsync(new SettingKey<Point>("Oxygen.Editor.Data.Example", nameof(moduleSettings.WindowPosition)), new Point(33, 44), ct: this.CancellationToken).ConfigureAwait(false);
            await mgr.SaveSettingAsync(new SettingKey<Size>("Oxygen.Editor.Data.Example", nameof(moduleSettings.WindowSize)), new Size(1024, 768), ct: this.CancellationToken).ConfigureAwait(false);

            await moduleSettings.LoadAsync(mgr, ct: this.CancellationToken).ConfigureAwait(false);

            _ = moduleSettings.WindowPosition.Should().Be(new Point(33, 44));
            _ = moduleSettings.WindowSize.Should().Be(new Size(1024, 768));
        }
    }
}
