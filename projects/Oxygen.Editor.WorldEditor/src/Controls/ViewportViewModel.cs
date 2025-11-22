// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Windows.Input;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using DroidNet.Controls.Menus;
using Microsoft.Extensions.Logging;
using Microsoft.UI.Xaml.Controls;

namespace Oxygen.Editor.WorldEditor.Controls;

/// <summary>
/// Defines the type of camera projection or orientation.
/// </summary>
public enum CameraType
{
    /// <summary>Perspective projection.</summary>
    Perspective,

    /// <summary>Top orthographic view.</summary>
    Top,

    /// <summary>Bottom orthographic view.</summary>
    Bottom,

    /// <summary>Left orthographic view.</summary>
    Left,

    /// <summary>Right orthographic view.</summary>
    Right,

    /// <summary>Front orthographic view.</summary>
    Front,

    /// <summary>Back orthographic view.</summary>
    Back,
}

/// <summary>
/// Defines the shading mode for the viewport.
/// </summary>
public enum ShadingMode
{
    /// <summary>Wireframe rendering.</summary>
    Wireframe,

    /// <summary>Shaded rendering.</summary>
    Shaded,

    /// <summary>Rendered mode.</summary>
    Rendered,
}

/// <summary>
/// ViewModel for the Viewport control, managing camera, shading, and menus.
/// </summary>
public partial class ViewportViewModel : ObservableObject
{
    private readonly ILoggerFactory? loggerFactory;

    /// <summary>
    /// Initializes a new instance of the <see cref="ViewportViewModel"/> class.
    /// </summary>
    /// <param name="loggerFactory">The logger factory.</param>
    public ViewportViewModel(ILoggerFactory? loggerFactory = null)
    {
        this.loggerFactory = loggerFactory;
        this.ViewMenu = this.BuildViewMenu();
        this.ShadingMenu = this.BuildShadingMenu();
    }

    /// <summary>
    /// Gets or sets the camera type.
    /// </summary>
    [ObservableProperty]
    public partial CameraType CameraType { get; set; } = CameraType.Perspective;

    /// <summary>
    /// Gets or sets the shading mode.
    /// </summary>
    [ObservableProperty]
    public partial ShadingMode ShadingMode { get; set; } = ShadingMode.Wireframe;

    /// <summary>
    /// Gets or sets a value indicating whether the viewport is maximized.
    /// </summary>
    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(MaximizeGlyph))]
    public partial bool IsMaximized { get; set; }

    /// <summary>
    /// Gets the menu source for the View menu.
    /// </summary>
    public IMenuSource ViewMenu { get; }

    /// <summary>
    /// Gets the menu source for the Shading menu.
    /// </summary>
    public IMenuSource ShadingMenu { get; }

    /// <summary>
    /// Gets or sets the command to toggle maximize state.
    /// </summary>
    public ICommand? ToggleMaximizeCommand { get; set; }

    /// <summary>
    /// Gets the glyph for the maximize/restore button.
    /// </summary>
    public string MaximizeGlyph => this.IsMaximized ? "\uE923" : "\uE922";

    private IMenuSource BuildViewMenu()
    {
        var builder = new MenuBuilder(this.loggerFactory);

        builder.AddRadioMenuItem("Perspective", "CameraType", this.CameraType == CameraType.Perspective, new RelayCommand(() => this.CameraType = CameraType.Perspective));
        builder.AddSeparator();
        builder.AddRadioMenuItem("Top", "CameraType", this.CameraType == CameraType.Top, new RelayCommand(() => this.CameraType = CameraType.Top), null);
        builder.AddRadioMenuItem("Bottom", "CameraType", this.CameraType == CameraType.Bottom, new RelayCommand(() => this.CameraType = CameraType.Bottom), null);
        builder.AddRadioMenuItem("Left", "CameraType", this.CameraType == CameraType.Left, new RelayCommand(() => this.CameraType = CameraType.Left), null);
        builder.AddRadioMenuItem("Right", "CameraType", this.CameraType == CameraType.Right, new RelayCommand(() => this.CameraType = CameraType.Right), null);
        builder.AddRadioMenuItem("Front", "CameraType", this.CameraType == CameraType.Front, new RelayCommand(() => this.CameraType = CameraType.Front), null);
        builder.AddRadioMenuItem("Back", "CameraType", this.CameraType == CameraType.Back, new RelayCommand(() => this.CameraType = CameraType.Back), null);

        return builder.Build();
    }

    private IMenuSource BuildShadingMenu()
    {
        var builder = new MenuBuilder(this.loggerFactory);

        builder.AddRadioMenuItem("Wireframe", "ShadingMode", this.ShadingMode == ShadingMode.Wireframe, new RelayCommand(() => this.ShadingMode = ShadingMode.Wireframe));
        builder.AddRadioMenuItem("Shaded", "ShadingMode", this.ShadingMode == ShadingMode.Shaded, new RelayCommand(() => this.ShadingMode = ShadingMode.Shaded));
        builder.AddRadioMenuItem("Rendered", "ShadingMode", this.ShadingMode == ShadingMode.Rendered, new RelayCommand(() => this.ShadingMode = ShadingMode.Rendered));

        return builder.Build();
    }
}
