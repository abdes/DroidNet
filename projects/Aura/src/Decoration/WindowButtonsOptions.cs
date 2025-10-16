// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Aura.Decoration;

/// <summary>
/// Immutable configuration options for window control button visibility and placement.
/// </summary>
/// <remarks>
/// <para>
/// Window button options control which standard window control buttons (minimize, maximize, close)
/// are displayed and where they are positioned in the title bar.
/// </para>
/// <para>
/// Use the <see cref="Default"/> property for standard Windows button configuration, or create
/// custom instances using object initializer syntax with init-only properties.
/// </para>
/// </remarks>
/// <example>
/// <code>
/// // Using default options (all buttons visible, right-aligned)
/// var options = WindowButtonsOptions.Default;
///
/// // Tool window with no maximize button
/// var toolOptions = WindowButtonsOptions.Default with
/// {
///     ShowMaximize = false,
/// };
///
/// // macOS-style button placement
/// var macStyleOptions = WindowButtonsOptions.Default with
/// {
///     Placement = ButtonPlacement.Left,
/// };
/// </code>
/// </example>
public sealed record WindowButtonsOptions
{
    /// <summary>
    /// Gets the default window button options with all buttons visible and right-aligned.
    /// </summary>
    public static readonly WindowButtonsOptions Default = new();

    /// <summary>
    /// Gets a value indicating whether the minimize button is displayed.
    /// </summary>
    /// <value>
    /// <see langword="true"/> to show the minimize button; otherwise, <see langword="false"/>.
    /// Default is <see langword="true"/>.
    /// </value>
    public bool ShowMinimize { get; init; } = true;

    /// <summary>
    /// Gets a value indicating whether the maximize/restore button is displayed.
    /// </summary>
    /// <value>
    /// <see langword="true"/> to show the maximize button; otherwise, <see langword="false"/>.
    /// Default is <see langword="true"/>.
    /// </value>
    public bool ShowMaximize { get; init; } = true;

    /// <summary>
    /// Gets a value indicating whether the close button is displayed.
    /// </summary>
    /// <value>
    /// <see langword="true"/> to show the close button; otherwise, <see langword="false"/>.
    /// Default is <see langword="true"/>.
    /// </value>
    /// <remarks>
    /// Primary windows should always have a close button to ensure proper application shutdown.
    /// The validation logic enforces this constraint.
    /// </remarks>
    public bool ShowClose { get; init; } = true;

    /// <summary>
    /// Gets the horizontal placement of the window buttons within the title bar.
    /// </summary>
    /// <value>
    /// A <see cref="ButtonPlacement"/> value. Default is <see cref="ButtonPlacement.Right"/>.
    /// </value>
    public ButtonPlacement Placement { get; init; } = ButtonPlacement.Right;
}
