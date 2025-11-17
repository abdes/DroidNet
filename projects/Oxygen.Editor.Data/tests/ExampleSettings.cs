// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.ComponentModel;
using System.ComponentModel.DataAnnotations;
using System.Drawing;
using Oxygen.Editor.Data.Models;
using Oxygen.Editor.Data.Validation;

namespace Oxygen.Editor.Data.Tests;

/// <summary>
/// Example concrete settings class for testing the descriptor registration pattern.
/// </summary>
/// <remarks>
/// This class demonstrates how a settings class with Persisted properties
/// will work with source-generated descriptors. In production, each module will have
/// its own concrete settings class.
/// </remarks>
public sealed partial class ExampleSettings : ModuleSettings
{
    private new const string ModuleName = "Oxygen.Editor.Data.Example";

    private Point windowPosition = new(0, 0);
    private Size windowSize = new(1200, 800);

    /// <summary>
    /// Initializes a new instance of the <see cref="ExampleSettings"/> class.
    /// </summary>
    public ExampleSettings()
        : base(ModuleName)
    {
    }

    /// <summary>
    /// Gets or sets the position of the window.
    /// </summary>
    [Persisted]
    [Display(Name = "Window Position", Description = "The position of the window")]
    [Category("Layout")]
    [PointBounds(0, int.MaxValue, 0, int.MaxValue, ErrorMessage = "X and Y coordinates must be non-negative.")]
    public Point WindowPosition
    {
        get => this.windowPosition;
        set => this.SetProperty(ref this.windowPosition, value);
    }

    /// <summary>
    /// Gets or sets the size of the window.
    /// </summary>
    [Persisted]
    [Display(Name = "Window Size", Description = "The size of the window")]
    [Category("Layout")]
    [SizeBounds(1, int.MaxValue, 1, int.MaxValue, ErrorMessage = "Width and Height must be positive.")]
    public Size WindowSize
    {
        get => this.windowSize;
        set => this.SetProperty(ref this.windowSize, value);
    }
}
