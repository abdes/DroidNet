// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.ComponentModel.DataAnnotations;
using System.Diagnostics.CodeAnalysis;
using System.Drawing;
using DryIoc;
using FluentAssertions;
using Oxygen.Editor.Data.Models;

namespace Oxygen.Editor.Data.Tests;

[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory("Windowed Module Settings")]
public class WindowedModuleSettingsTests : DatabaseTests
{
    /// <summary>
    /// Initializes a new instance of the <see cref="WindowedModuleSettingsTests"/> class.
    /// </summary>
    public WindowedModuleSettingsTests()
    {
        this.Container.Register<EditorSettingsManager>(Reuse.Scoped);
    }

    [TestMethod]
    public void WindowPositionShouldHavePersistedAttribute()
    {
        var property = typeof(WindowedModuleSettings).GetProperty(nameof(WindowedModuleSettings.WindowPosition));
        var attribute = property?.GetCustomAttributes(typeof(PersistedAttribute), false).FirstOrDefault();
        _ = attribute.Should().NotBeNull();
    }

    [TestMethod]
    public void WindowSizeShouldHavePersistedAttribute()
    {
        var property = typeof(WindowedModuleSettings).GetProperty(nameof(WindowedModuleSettings.WindowSize));
        var attribute = property?.GetCustomAttributes(typeof(PersistedAttribute), false).FirstOrDefault();
        _ = attribute.Should().NotBeNull();
    }

    [TestMethod]
    public void WindowPositionShouldValidateNonNegativeCoordinates()
    {
        var scope = this.Container.OpenScope();
        using (scope)
        {
            var settingsManager = scope.Resolve<EditorSettingsManager>();
            var moduleSettings = new TestWindowedModuleSettings(settingsManager, "TestModule");

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
            var moduleSettings = new TestWindowedModuleSettings(settingsManager, "TestModule");

            Action act = () => moduleSettings.WindowSize = new Size(0, 0);

            _ = act.Should().Throw<ValidationException>().WithMessage("Width and Height must be positive.");
        }
    }

    private sealed class TestWindowedModuleSettings(EditorSettingsManager settingsManager, string moduleName)
        : WindowedModuleSettings(settingsManager, moduleName);
}
