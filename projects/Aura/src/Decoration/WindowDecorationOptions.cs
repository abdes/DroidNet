// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Aura.Decoration;

/// <summary>
///     Provides immutable configuration options for window decoration, including chrome, title bar,
///     buttons, menu, and backdrop.
/// </summary>
/// <remarks>
///     <see cref="WindowDecorationOptions"/> enables strongly-typed, immutable configuration for
///     customizing the appearance and behavior of Aura-managed windows. It supports preset-based
///     defaults and fluent builder APIs for advanced customization.
/// <para>
///     All properties are init-only and validated for consistency. Use <see
///     cref="WindowDecorationBuilder"/> for standard configurations, or <c>with</c> expressions for
///     small customizations. Always call <see cref="Validate"/> after modifications.
/// </para>
/// </remarks>
public sealed record WindowDecorationOptions
{
    /// <summary>
    ///     Gets the semantic category of the window for grouping and default decoration selection.
    /// </summary>
    /// <value>
    ///     A non-empty <see cref="WindowCategory"/> identifying the window category (e.g., Main,
    ///     Tool, Document).
    /// </value>
    /// <remarks>
    ///     Used for applying category-specific defaults, grouping windows, and enforcing validation
    ///     rules (e.g., primary windows require a Close button).
    /// </remarks>
    public required WindowCategory Category { get; init; } // TODO: do not serialize

    /// <summary>
    ///     Gets a value indicating whether Aura-provided window chrome is enabled.
    /// </summary>
    /// <value>
    ///     <see langword="true"/> to use Aura chrome with custom title bar; <see langword="false"/>
    ///     to use system-provided window chrome. Default is <see langword="true"/>.
    /// </value>
    /// <remarks>
    ///     When <see langword="false"/> is specified, most other decoration options are ignored.
    ///     Menus require <see langword="true"/> to be displayed.
    /// </remarks>
    public bool ChromeEnabled { get; init; } = true;

    /// <summary>
    ///     Gets a value indicating whether a border is drawn around the window.
    /// </summary>
    /// <value>
    ///     <see langword="true"/> to draw a border; otherwise, <see langword="false"/>. Default is
    ///     <see langword="true"/>.
    /// </value>
    public bool WithBorder { get; init; } = true;

    /// <summary>
    ///     Gets a value indicating whether the winbdow will have rounded corners.
    /// </summary>
    /// <value>
    ///     <see langword="true"/> to enable rounded corners; otherwise, <see langword="false"/>. Default is
    ///     <see langword="true"/>.
    /// </value>
    public bool RoundedCorners { get; init; } = true;

    /// <summary>
    ///     Gets a value indicating whether the window can be resized by the user.
    /// </summary>
    /// <value>
    ///     <see langword="true"/> if the window is resizable; otherwise, <see langword="false"/>.
    ///     Default is <see langword="true"/>.
    /// </value>
    public bool IsResizable { get; init; } = true;

    /// <summary>
    ///     Gets the title bar configuration options.
    /// </summary>
    /// <value>
    ///     A <see cref="TitleBarOptions"/> instance, or <see langword="null"/> to use defaults. Only applied
    ///     when <see cref="ChromeEnabled"/> is <see langword="true"/>.
    /// </value>
    public TitleBarOptions? TitleBar { get; init; }

    /// <summary>
    ///     Gets the window control buttons configuration options.
    /// </summary>
    /// <value>
    ///     A <see cref="WindowButtonsOptions"/> instance. Default is <see
    ///     cref="WindowButtonsOptions.Default"/>.
    /// </value>
    /// <remarks>
    ///     Only applied when <see cref="ChromeEnabled"/> is <see langword="true"/>. Primary windows
    ///     must have the Close button enabled.
    /// </remarks>
    public WindowButtonsOptions Buttons { get; init; } = WindowButtonsOptions.Default;

    /// <summary>
    ///     Gets the menu configuration options, or <see langword="null"/> if no menu should be displayed.
    /// </summary>
    /// <value>
    ///     A <see cref="MenuOptions"/> instance for menu configuration, or <see langword="null"/> for no menu.
    ///     Default is <see langword="null"/>.
    /// </value>
    /// <remarks>
    ///     Menus require <see cref="ChromeEnabled"/> to be <see langword="true"/>. The menu
    ///     provider is resolved from DI using the provider ID.
    /// </remarks>
    public MenuOptions? Menu { get; init; }

    /// <summary>
    ///     Gets the backdrop material effect override for this window.
    /// </summary>
    /// <value>
    ///     A <see cref="BackdropKind"/> value. Default is <see cref="BackdropKind.None"/>.
    /// </value>
    /// <remarks>
    ///     Set to <see cref="BackdropKind.None"/> to disable backdrop for this window.
    /// </remarks>
    public BackdropKind Backdrop { get; init; } = BackdropKind.None;

    /// <summary>
    ///     Validates the decoration options and throws an exception if the configuration is invalid.
    /// </summary>
    /// <exception cref="ValidationException">
    ///     Thrown if:
    ///     <list type="bullet">
    ///     <item><see cref="Category"/> is empty or whitespace</item>
    ///     <item><see cref="Menu"/> is specified when <see cref="ChromeEnabled"/> is <see langword="false"/></item>
    ///     <item>Menu provider ID is empty or whitespace when <see cref="Menu"/> is specified</item>
    ///     <item><see cref="TitleBarOptions.Height"/> is zero or negative</item>
    ///     </list>
    /// </exception>
    /// <remarks>
    ///     Always call this method after creating or modifying instances. <see cref="WindowDecorationBuilder.Build"/>
    ///     calls this automatically.
    /// </remarks>
    public void Validate()
    {
        // Validate ChromeEnabled + Menu combination
        if (!this.ChromeEnabled && this.Menu is not null)
        {
            throw new ValidationException(
                "Menu cannot be specified when ChromeEnabled is false. Menus require Aura chrome. " +
                "Either set ChromeEnabled to true or remove the Menu configuration.");
        }

        // Validate TitleBar.Height
        if (this.TitleBar?.Height <= 0.0)
        {
            throw new ValidationException(
                string.Format(
                    System.Globalization.CultureInfo.InvariantCulture,
                    "Title bar height must be greater than zero. Current value: {0}",
                    this.TitleBar.Height));
        }

        // Validate MenuProviderId if Menu is specified
        if (this.Menu is not null && string.IsNullOrWhiteSpace(this.Menu.MenuProviderId))
        {
            throw new ValidationException(
                "Menu provider ID cannot be empty or whitespace when Menu is specified.");
        }
    }
}
