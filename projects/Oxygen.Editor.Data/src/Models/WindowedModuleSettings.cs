// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.ComponentModel.DataAnnotations;
using System.Diagnostics.CodeAnalysis;
using System.Drawing;

namespace Oxygen.Editor.Data.Models;

/// <summary>
/// Represents the settings for a windowed module, including window position and size.
/// </summary>
/// <remarks>
/// The <see cref="WindowedModuleSettings"/> class extends the <see cref="ModuleSettings"/> class to
/// include properties for managing the position and size of a window. These properties are
/// automatically persisted and retrieved using the <see cref="SettingsManager"/> when the
/// <see cref="ModuleSettings.SaveAsync"/> and <see cref="ModuleSettings.LoadAsync"/> methods are called.
/// <para>
/// The <see cref="WindowPosition"/> property represents the coordinates of the window's top-left corner,
/// and it must be non-negative. The <see cref="WindowSize"/> property represents the dimensions of the
/// window, and it must be positive.
/// </para>
/// </remarks>
/// <example>
/// <para><strong>Example Usage</strong></para>
/// <![CDATA[
/// public class MyWindowedModuleSettings : WindowedModuleSettings
/// {
///     public MyWindowedModuleSettings(SettingsManager settingsManager, string moduleName)
///         : base(settingsManager, moduleName)
///     {
///     }
/// }
///
/// // Usage
/// var settingsManager = new SettingsManager(context);
/// var mySettings = new MyWindowedModuleSettings(settingsManager, "MyWindowedModule");
/// await mySettings.LoadAsync();
/// mySettings.WindowPosition = new Point(100, 100);
/// mySettings.WindowSize = new Size(1024, 768);
/// await mySettings.SaveAsync();
/// ]]>
/// </example>
public abstract class WindowedModuleSettings : ModuleSettings
{
    private Point windowPosition = new(0, 0);
    private Size windowSize = new(1200, 800);

    /// <summary>
    /// Initializes a new instance of the <see cref="WindowedModuleSettings"/> class.
    /// </summary>
    /// <param name="settingsManager">The settings manager responsible for persisting settings.</param>
    /// <param name="moduleName">The name of the module.</param>
    /// <remarks>
    /// This constructor initializes the base <see cref="ModuleSettings"/> class and sets the default window position and size.
    /// </remarks>
    [SuppressMessage("ReSharper", "ConvertToPrimaryConstructor", Justification = "don't want to capture settingsManager, which is also captured in the base class")]
    [SuppressMessage("Style", "IDE0290:Use primary constructor", Justification = "Don't want to capture settingsManager, which is also captured in the base class")]
    protected WindowedModuleSettings(ISettingsManager settingsManager, string moduleName)
        : base(settingsManager, moduleName)
    {
    }

    /// <summary>
    /// Gets or sets the position of the window.
    /// </summary>
    /// <value>
    /// A <see cref="Point"/> representing the coordinates of the window's top-left corner.
    /// </value>
    /// <exception cref="ValidationException">
    /// Thrown when the coordinates are negative.
    /// </exception>
    [Persisted]
    [Range(0, int.MaxValue, ErrorMessage = "X and Y coordinates must be non-negative.")]
    public Point WindowPosition
    {
        get => this.windowPosition;
        set => this.SetProperty(ref this.windowPosition, value);
    }

    /// <summary>
    /// Gets or sets the size of the window.
    /// </summary>
    /// <value>
    /// A <see cref="Size"/> representing the dimensions of the window.
    /// </value>
    /// <exception cref="ValidationException">
    /// Thrown when the width or height is not positive.
    /// </exception>
    [Persisted]
    [Range(1, int.MaxValue, ErrorMessage = "Width and Height must be positive.")]
    public Size WindowSize
    {
        get => this.windowSize;
        set => this.SetProperty(ref this.windowSize, value);
    }
}
