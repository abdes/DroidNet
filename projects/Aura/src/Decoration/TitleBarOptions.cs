// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.UI.Xaml;

namespace DroidNet.Aura.Decoration;

/// <summary>
/// Immutable configuration options for a window's title bar appearance and behavior.
/// </summary>
/// <remarks>
/// <para>
/// Title bar options control the visual presentation and interaction behavior of the window's
/// title bar area. This includes dimensions, content visibility, and drag behavior.
/// </para>
/// <para>
/// Use the <see cref="Default"/> property for standard title bar configuration, or create
/// custom instances using object initializer syntax with init-only properties.
/// </para>
/// </remarks>
/// <example>
/// <code>
/// // Using default options
/// var options = TitleBarOptions.Default;
///
/// // Customizing specific properties
/// var customOptions = TitleBarOptions.Default with
/// {
///     Height = 40.0,
///     ShowIcon = false,
/// };
/// </code>
/// </example>
public sealed record TitleBarOptions
{
    /// <summary>
    /// Gets the default title bar options with standard Windows styling.
    /// </summary>
    public static readonly TitleBarOptions Default = new();

    /// <summary>
    /// Gets the height of the title bar in device-independent pixels.
    /// </summary>
    /// <value>
    /// The title bar height. Default is 32.0 pixels. Must be greater than zero.
    /// </value>
    public double Height { get; init; } = 32.0;

    /// <summary>
    /// Gets a value indicating whether the window title text is displayed in the title bar.
    /// </summary>
    /// <value>
    /// <see langword="true"/> to display the window title; otherwise, <see langword="false"/>.
    /// Default is <see langword="true"/>.
    /// </value>
    public bool ShowTitle { get; init; } = true;

    /// <summary>
    /// Gets a value indicating whether the window icon is displayed in the title bar.
    /// </summary>
    /// <value>
    /// <see langword="true"/> to display the window icon; otherwise, <see langword="false"/>.
    /// Default is <see langword="true"/>.
    /// </value>
    public bool ShowIcon { get; init; } = true;

    /// <summary>
    /// Gets the drag region behavior for window movement interaction.
    /// </summary>
    /// <value>
    /// A <see cref="DragRegionBehavior"/> value. Default is <see cref="DragRegionBehavior.Default"/>.
    /// </value>
    public DragRegionBehavior DragBehavior { get; init; } = DragRegionBehavior.Default;
}
