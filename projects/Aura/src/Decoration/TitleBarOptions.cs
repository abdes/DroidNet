// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Aura.Decoration;

/// <summary>
///     Immutable configuration options for a window's title bar appearance and behavior.
/// </summary>
/// <remarks>
///     Title bar options control the visual presentation and interaction behavior of the window's
///     title bar area. This includes dimensions, content visibility, and drag behavior.
///     <para>
///     Use the <see cref="Default"/> property for standard title bar configuration, or create
///     custom instances using object initializer syntax with init-only properties.
///     </para>
/// </remarks>
/// <example>
///     <code><![CDATA[
///     // Using default options
///     var options = TitleBarOptions.Default;
///
///     // Customizing specific properties
///     var customOptions = TitleBarOptions.Default with
///     {
///         Height = 40.0,
///         ShowIcon = false,
///     };
///     ]]></code>
/// </example>
public sealed record TitleBarOptions
{
    /// <summary>
    ///     Gets the default title bar options with standard Windows styling.
    /// </summary>
    public static readonly TitleBarOptions Default = new();

    /// <summary>
    ///     Gets the height of the title bar in device-independent pixels.
    /// </summary>
    /// <value>
    ///     The title bar height. Default is 32.0 pixels. Must be greater than zero.
    /// </value>
    public double Height { get; init; } = 32.0;

    /// <summary>
    ///     Gets a value indicating whether the window title text is displayed in the title bar.
    /// </summary>
    /// <value>
    ///     <see langword="true"/> to display the window title; otherwise, <see langword="false"/>.
    ///     Default is <see langword="true"/>.
    /// </value>
    public bool ShowTitle { get; init; } = true;

    /// <summary>
    ///     Gets a value indicating whether the window icon is displayed in the title bar.
    /// </summary>
    /// <value>
    ///     <see langword="true"/> to display the window icon; otherwise, <see langword="false"/>.
    ///     Default is <see langword="true"/>.
    /// </value>
    public bool ShowIcon { get; init; } = true;

    /// <summary>
    ///     Gets a value indicating whether document tabs are shown in the title bar area.
    /// </summary>
    /// <value>
    ///     <see langword="true"/> to show document tabs; otherwise, <see langword="false"/>.
    ///     Default is <see langword="false"/>.
    /// </value>
    public bool WithDocumentTabs { get; init; }

    /// <summary>
    ///     Gets the relative path to the window icon file, resolved at runtime by looking for an
    ///     asset under the application's `Assets` directory, or in Aura's assembly Assets.
    /// </summary>
    /// <value>
    ///    The icon file path. Default is `"DroidNet.png"`.
    /// </value>
    public string IconPath { get; init; } = "DroidNet.png";

    /// <summary>
    ///     Gets the drag region behavior for window movement interaction.
    /// </summary>
    /// <value>
    ///     A <see cref="DragRegionBehavior"/> value. Default is <see cref="DragRegionBehavior.Default"/>.
    /// </value>
    public DragRegionBehavior DragBehavior { get; init; } = DragRegionBehavior.Default;
}
