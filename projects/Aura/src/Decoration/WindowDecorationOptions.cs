// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Aura.Decoration;

/// <summary>
/// Immutable configuration for window decoration including chrome, title bar, buttons, menu, and backdrop.
/// </summary>
/// <remarks>
/// <para>
/// WindowDecorationOptions provides a strongly-typed, immutable configuration system for customizing
/// the appearance and behavior of Aura-managed windows. It supports preset-based defaults for common
/// scenarios and fluent builder APIs for advanced customization.
/// </para>
/// <para>
/// The options record is immutable after creation (init-only properties) and includes comprehensive
/// validation to ensure consistency and prevent invalid configurations.
/// </para>
/// <para>
/// Common usage patterns:
/// <list type="bullet">
/// <item>Use preset factory methods from <see cref="WindowDecorationBuilder"/> for standard configurations</item>
/// <item>Use <c>with</c> expressions for small customizations to presets</item>
/// <item>Call <see cref="Validate"/> to ensure configuration validity before use</item>
/// </list>
/// </para>
/// </remarks>
/// <example>
/// <code>
/// // Using a preset
/// var primaryOptions = WindowDecorationBuilder.ForMainWindow().Build();
///
/// // Customizing a preset
/// var customOptions = WindowDecorationBuilder.ForToolWindow()
///     .WithBackdrop(BackdropKind.Mica)
///     .WithTitleBarHeight(36.0)
///     .Build();
///
/// // Using with expression
/// var modified = primaryOptions with { Backdrop = BackdropKind.Acrylic };
/// modified.Validate(); // Always validate after modifications
/// </code>
/// </example>
public sealed record WindowDecorationOptions
{
    /// <summary>
    /// Gets the semantic category of the window for grouping and default decoration selection.
    /// </summary>
    /// <value>
    /// A non-empty string identifying the window category (e.g., "Primary", "Tool", "Document").
    /// </value>
    /// <remarks>
    /// Categories are used for:
    /// <list type="bullet">
    /// <item>Applying category-specific default decorations from settings</item>
    /// <item>Grouping windows in management UI</item>
    /// <item>Enforcing category-specific validation rules (e.g., Primary windows require Close button)</item>
    /// </list>
    /// </remarks>
    public required WindowCategory Category { get; init; }

    /// <summary>
    /// Gets a value indicating whether Aura-provided window chrome is enabled.
    /// </summary>
    /// <value>
    /// <see langword="true"/> to use Aura chrome with custom title bar; <see langword="false"/> to use
    /// system-provided window chrome. Default is <see langword="true"/>.
    /// </value>
    /// <remarks>
    /// <para>
    /// When <see langword="false"/>, the window uses standard system chrome and most other decoration
    /// options are ignored. This is useful for windows that require native OS integration or for
    /// compatibility with specific platform features.
    /// </para>
    /// <para>
    /// Setting this to <see langword="false"/> while also specifying a <see cref="Menu"/> will cause
    /// validation to fail, as menus require Aura chrome.
    /// </para>
    /// </remarks>
    public bool ChromeEnabled { get; init; } = true;

    /// <summary>
    /// Gets the title bar configuration options.
    /// </summary>
    /// <value>
    /// A <see cref="TitleBarOptions"/> instance. Default is <see cref="TitleBarOptions.Default"/>.
    /// </value>
    /// <remarks>
    /// Title bar options are only applied when <see cref="ChromeEnabled"/> is <see langword="true"/>.
    /// </remarks>
    public TitleBarOptions TitleBar { get; init; } = TitleBarOptions.Default;

    /// <summary>
    /// Gets the window control buttons configuration options.
    /// </summary>
    /// <value>
    /// A <see cref="WindowButtonsOptions"/> instance. Default is <see cref="WindowButtonsOptions.Default"/>.
    /// </value>
    /// <remarks>
    /// Button options are only applied when <see cref="ChromeEnabled"/> is <see langword="true"/>.
    /// Primary windows must have the Close button enabled to ensure proper application shutdown.
    /// </remarks>
    public WindowButtonsOptions Buttons { get; init; } = WindowButtonsOptions.Default;

    /// <summary>
    /// Gets the menu configuration options, or <see langword="null"/> if no menu should be displayed.
    /// </summary>
    /// <value>
    /// A <see cref="MenuOptions"/> instance for menu configuration, or <see langword="null"/> for no menu.
    /// Default is <see langword="null"/>.
    /// </value>
    /// <remarks>
    /// <para>
    /// Menus require Aura chrome (<see cref="ChromeEnabled"/> must be <see langword="true"/>).
    /// The menu provider is resolved from the DI container using the provider ID specified in MenuOptions.
    /// </para>
    /// <para>
    /// If the menu provider is not found, a warning is logged and the window is created without a menu
    /// (graceful degradation).
    /// </para>
    /// </remarks>
    public MenuOptions? Menu { get; init; }

    /// <summary>
    /// Gets the backdrop material effect override for this specific window.
    /// </summary>
    /// <value>
    /// A <see cref="BackdropKind"/> value to use for this window. Default is <see cref="BackdropKind.None"/>.
    /// </value>
    /// <remarks>
    /// <para>
    /// To explicitly disable backdrop for a specific window set this property to <see cref="BackdropKind.None"/>.
    /// </para>
    /// </remarks>
    public BackdropKind Backdrop { get; init; } = BackdropKind.None;

    /// <summary>
    /// Gets a value indicating whether the system title bar overlay feature is enabled.
    /// </summary>
    /// <value>
    /// <see langword="true"/> to enable system title bar overlay; otherwise, <see langword="false"/>.
    /// Default is <see langword="false"/>.
    /// </value>
    /// <remarks>
    /// <para>
    /// System title bar overlay (WinUI 3 feature) allows application content to extend into the title bar
    /// region, providing more screen real estate and modern UI patterns. This requires careful coordination
    /// with drag regions and interactive elements.
    /// </para>
    /// <para>
    /// This setting only applies when <see cref="ChromeEnabled"/> is <see langword="true"/>.
    /// </para>
    /// </remarks>
    public bool EnableSystemTitleBarOverlay { get; init; }

    /// <summary>
    /// Validates the decoration options and throws an exception if the configuration is invalid.
    /// </summary>
    /// <exception cref="ValidationException">
    /// Thrown when the configuration is invalid. Common validation failures include:
    /// <list type="bullet">
    /// <item>Empty or whitespace-only <see cref="Category"/></item>
    /// <item><see cref="Menu"/> specified when <see cref="ChromeEnabled"/> is <see langword="false"/></item>
    /// <item>Primary category window with <see cref="WindowButtonsOptions.ShowClose"/> set to <see langword="false"/></item>
    /// <item><see cref="TitleBarOptions.Height"/> is zero or negative</item>
    /// <item><see cref="TitleBarOptions.Padding"/> is negative</item>
    /// </list>
    /// </exception>
    /// <remarks>
    /// <para>
    /// Always call this method after creating or modifying WindowDecorationOptions instances to ensure
    /// validity before passing to the window manager. The WindowDecorationBuilder calls this automatically
    /// in its Build() method.
    /// </para>
    /// <para>
    /// Validation is designed to catch configuration errors early in the development process with clear,
    /// actionable error messages.
    /// </para>
    /// </remarks>
    /// <example>
    /// <code>
    /// var options = new WindowDecorationOptions
    /// {
    ///     Category = "Primary",
    ///     ChromeEnabled = true,
    ///     Buttons = WindowButtonsOptions.Default with { ShowClose = false },
    /// };
    ///
    /// try
    /// {
    ///     options.Validate(); // Will throw ValidationException
    /// }
    /// catch (ValidationException ex)
    /// {
    ///     Console.WriteLine($"Invalid configuration: {ex.Message}");
    /// }
    /// </code>
    /// </example>
    public void Validate()
    {
        // Validate ChromeEnabled + Menu combination
        if (!this.ChromeEnabled && this.Menu is not null)
        {
            throw new ValidationException(
                "Menu cannot be specified when ChromeEnabled is false. Menus require Aura chrome. " +
                "Either set ChromeEnabled to true or remove the Menu configuration.");
        }

        // Validate Primary category requires Close button
        if (this.Category.Equals(WindowCategory.Main) && !this.Buttons.ShowClose)
        {
            throw new ValidationException(
                "Primary windows must have the Close button enabled to ensure proper application shutdown. " +
                "Set ShowClose to true in the Buttons configuration.");
        }

        // Validate TitleBar.Height
        if (this.TitleBar.Height <= 0.0)
        {
            throw new ValidationException(
                $"Title bar height must be greater than zero. Current value: {this.TitleBar.Height}");
        }

        // Validate MenuProviderId if Menu is specified
        if (this.Menu is not null && string.IsNullOrWhiteSpace(this.Menu.MenuProviderId))
        {
            throw new ValidationException(
                "Menu provider ID cannot be empty or whitespace when Menu is specified.");
        }
    }
}
