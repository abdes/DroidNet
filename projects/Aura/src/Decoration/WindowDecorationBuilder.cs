// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Aura.Decoration;

/// <summary>
/// Fluent builder for creating <see cref="WindowDecorationOptions"/> with preset factory methods
/// and customization capabilities.
/// </summary>
/// <remarks>
/// <para>
/// The builder provides preset factory methods for common window types (Primary, Document, Tool,
/// Secondary, SystemChrome) that configure sensible defaults suitable for 90% of use cases.
/// Each preset can be further customized using fluent methods before calling <see cref="Build"/>
/// to create an immutable <see cref="WindowDecorationOptions"/> instance.
/// </para>
/// <para>
/// All fluent methods return the same builder instance to enable method chaining. The
/// <see cref="Build"/> method validates the configuration and throws
/// <see cref="ValidationException"/> if invalid combinations are detected.
/// </para>
/// </remarks>
/// <example>
/// <code>
/// // Use a preset with default settings
/// var primaryDecoration = WindowDecorationBuilder.ForMainWindow().Build();
///
/// // Customize a preset
/// var customTool = WindowDecorationBuilder.ForToolWindow()
///     .WithTitleBarHeight(40.0)
///     .WithMenu("App.ToolMenu", isCompact: true)
///     .Build();
///
/// // Build from scratch
/// var custom = new WindowDecorationBuilder()
///     .WithCategory("Custom")
///     .WithChrome(true)
///     .WithBackdrop(BackdropKind.Mica)
///     .WithMenu("App.MainMenu")
///     .Build();
/// </code>
/// </example>
public sealed class WindowDecorationBuilder
{
    /// <summary>
    /// Default backdrop per window category.
    /// </summary>
    private static readonly Dictionary<WindowCategory, BackdropKind> DefaultBackdrops = new()
    {
        // Use Mica for primary surfaces
        [WindowCategory.Main] = BackdropKind.Mica,

        // Use MicaAlt for contrast (secondary/utility)
        [WindowCategory.Secondary] = BackdropKind.MicaAlt,
        [WindowCategory.Tool] = BackdropKind.MicaAlt,

        // Use Acrylic for transient/layered UI (e.g., floating inspectors)
        [WindowCategory.Transient] = BackdropKind.Acrylic,

        // Use Acrylic for modal dialogs, dense content
        [WindowCategory.Modal] = BackdropKind.Acrylic,
        [WindowCategory.Document] = BackdropKind.Mica,

        // Fallback -> None
        [WindowCategory.System] = BackdropKind.None,
    };

    private WindowCategory category = WindowCategory.System;
    private bool chromeEnabled = true;
    private TitleBarOptions titleBar = TitleBarOptions.Default;
    private WindowButtonsOptions buttons = WindowButtonsOptions.Default;
    private MenuOptions? menu;
    private BackdropKind backdrop = BackdropKind.None;
    private bool enableSystemTitleBarOverlay;

    /// <summary>
    /// Creates a builder for a primary application window.
    /// </summary>
    /// <returns>A builder configured with primary window defaults.</returns>
    /// <remarks>
    /// <para>
    /// Primary windows are the main application windows and are configured with:
    /// </para>
    /// <list type="bullet">
    /// <item><description>Category: "Primary"</description></item>
    /// <item><description>Chrome: Enabled</description></item>
    /// <item><description>Title bar height: 40px</description></item>
    /// <item><description>All buttons: Visible (minimize, maximize, close)</description></item>
    /// <item><description>Backdrop: MicaAlt for modern appearance</description></item>
    /// </list>
    /// </remarks>
    /// <example>
    /// <code>
    /// var decoration = WindowDecorationBuilder.ForMainWindow()
    ///     .WithMenu("App.MainMenu")
    ///     .Build();
    /// </code>
    /// </example>
    public static WindowDecorationBuilder ForMainWindow()
    {
        return new WindowDecorationBuilder
        {
            category = WindowCategory.Main,
            chromeEnabled = true,
            titleBar = TitleBarOptions.Default with { Height = 40.0 },
            buttons = WindowButtonsOptions.Default,
            backdrop = DefaultBackdrops[WindowCategory.Main],
        };
    }

    /// <summary>
    /// Creates a builder for a document window.
    /// </summary>
    /// <returns>A builder configured with document window defaults.</returns>
    /// <remarks>
    /// <para>
    /// Document windows are used for editing content and are configured with:
    /// </para>
    /// <list type="bullet">
    /// <item><description>Category: "Document"</description></item>
    /// <item><description>Chrome: Enabled</description></item>
    /// <item><description>Title bar: Standard height (32px)</description></item>
    /// <item><description>All buttons: Visible</description></item>
    /// <item><description>Backdrop: Mica for subtle appearance</description></item>
    /// </list>
    /// </remarks>
    /// <example>
    /// <code>
    /// var decoration = WindowDecorationBuilder.ForDocumentWindow()
    ///     .WithMenu("App.DocumentMenu")
    ///     .Build();
    /// </code>
    /// </example>
    public static WindowDecorationBuilder ForDocumentWindow()
    {
        return new WindowDecorationBuilder
        {
            category = WindowCategory.Document,
            chromeEnabled = true,
            titleBar = TitleBarOptions.Default,
            buttons = WindowButtonsOptions.Default,
            backdrop = DefaultBackdrops[WindowCategory.Document],
        };
    }

    /// <summary>
    /// Creates a builder for a tool window.
    /// </summary>
    /// <returns>A builder configured with tool window defaults.</returns>
    /// <remarks>
    /// <para>
    /// Tool windows are auxiliary windows like palettes and toolboxes, configured with:
    /// </para>
    /// <list type="bullet">
    /// <item><description>Category: "Tool"</description></item>
    /// <item><description>Chrome: Enabled</description></item>
    /// <item><description>Title bar height: 32px</description></item>
    /// <item><description>Maximize button: Hidden (tools typically don't maximize)</description></item>
    /// <item><description>Backdrop: null (uses application-wide default)</description></item>
    /// </list>
    /// </remarks>
    /// <example>
    /// <code>
    /// var decoration = WindowDecorationBuilder.ForToolWindow()
    ///     .WithMenu("App.ToolMenu", isCompact: true)
    ///     .Build();
    /// </code>
    /// </example>
    public static WindowDecorationBuilder ForToolWindow()
    {
        return new WindowDecorationBuilder
        {
            category = WindowCategory.Tool,
            chromeEnabled = true,
            titleBar = TitleBarOptions.Default,
            buttons = WindowButtonsOptions.Default with { ShowMaximize = false },
            backdrop = DefaultBackdrops[WindowCategory.Tool],
        };
    }

    /// <summary>
    /// Creates a builder for a secondary window.
    /// </summary>
    /// <returns>A builder configured with secondary window defaults.</returns>
    /// <remarks>
    /// <para>
    /// Secondary windows are additional content windows, configured with:
    /// </para>
    /// <list type="bullet">
    /// <item><description>Category: "Secondary"</description></item>
    /// <item><description>Chrome: Enabled</description></item>
    /// <item><description>Title bar: Standard height</description></item>
    /// <item><description>All buttons: Visible</description></item>
    /// <item><description>Backdrop: null (uses application-wide default)</description></item>
    /// </list>
    /// </remarks>
    /// <example>
    /// <code>
    /// var decoration = WindowDecorationBuilder.ForSecondaryWindow()
    ///     .WithBackdrop(BackdropKind.Acrylic)
    ///     .Build();
    /// </code>
    /// </example>
    public static WindowDecorationBuilder ForSecondaryWindow()
    {
        return new WindowDecorationBuilder
        {
            category = WindowCategory.Secondary,
            chromeEnabled = true,
            titleBar = TitleBarOptions.Default,
            buttons = WindowButtonsOptions.Default,
            backdrop = DefaultBackdrops[WindowCategory.Secondary],
        };
    }

    /// <summary>
    /// Creates a builder for a transient window.
    /// </summary>
    /// <returns>A builder configured with transient window defaults.</returns>
    /// <remarks>
    /// Transient windows are short-lived floating UI such as inspectors or popups and are
    /// configured to use a lightweight backdrop and standard titlebar.
    /// </remarks>
    public static WindowDecorationBuilder ForTransientWindow()
    {
        return new WindowDecorationBuilder
        {
            category = WindowCategory.Transient,
            chromeEnabled = true,
            titleBar = TitleBarOptions.Default,
            buttons = WindowButtonsOptions.Default with { ShowMaximize = false },
            backdrop = DefaultBackdrops[WindowCategory.Transient],
        };
    }

    /// <summary>
    /// Creates a builder for a modal window.
    /// </summary>
    /// <returns>A builder configured with modal window defaults.</returns>
    /// <remarks>
    /// Modal windows block interaction with other windows; they typically use acrylic
    /// to emphasize focus and may hide maximize controls.
    /// </remarks>
    public static WindowDecorationBuilder ForModalWindow()
    {
        return new WindowDecorationBuilder
        {
            category = WindowCategory.Modal,
            chromeEnabled = true,
            titleBar = TitleBarOptions.Default,
            buttons = WindowButtonsOptions.Default with { ShowMaximize = false },
            backdrop = DefaultBackdrops[WindowCategory.Modal],
        };
    }

    /// <summary>
    /// Sets the window category.
    /// </summary>
    /// <param name="category">The category identifier (e.g., "Primary", "Tool", "Document").</param>
    /// <returns>This builder instance for method chaining.</returns>
    /// <exception cref="ArgumentException">Thrown if category is null or empty.</exception>
    public WindowDecorationBuilder WithCategory(WindowCategory category)
    {
        this.category = category;
        return this;
    }

    /// <summary>
    /// Sets whether Aura chrome is enabled.
    /// </summary>
    /// <param name="enabled">True to enable Aura chrome, false to use system chrome.</param>
    /// <returns>This builder instance for method chaining.</returns>
    public WindowDecorationBuilder WithChrome(bool enabled)
    {
        this.chromeEnabled = enabled;
        return this;
    }

    /// <summary>
    /// Sets the title bar options.
    /// </summary>
    /// <param name="titleBar">The title bar configuration.</param>
    /// <returns>This builder instance for method chaining.</returns>
    /// <exception cref="ArgumentNullException">Thrown if titleBar is null.</exception>
    public WindowDecorationBuilder WithTitleBar(TitleBarOptions titleBar)
    {
        this.titleBar = titleBar ?? throw new ArgumentNullException(nameof(titleBar));
        return this;
    }

    /// <summary>
    /// Sets the window buttons options.
    /// </summary>
    /// <param name="buttons">The buttons configuration.</param>
    /// <returns>This builder instance for method chaining.</returns>
    /// <exception cref="ArgumentNullException">Thrown if buttons is null.</exception>
    public WindowDecorationBuilder WithButtons(WindowButtonsOptions buttons)
    {
        this.buttons = buttons ?? throw new ArgumentNullException(nameof(buttons));
        return this;
    }

    /// <summary>
    /// Sets the menu options.
    /// </summary>
    /// <param name="menu">The menu configuration, or null to display no menu.</param>
    /// <returns>This builder instance for method chaining.</returns>
    public WindowDecorationBuilder WithMenu(MenuOptions? menu)
    {
        this.menu = menu;
        return this;
    }

    /// <summary>
    /// Sets the menu options using a provider ID.
    /// </summary>
    /// <param name="providerId">The menu provider identifier.</param>
    /// <param name="isCompact">Whether to use compact menu mode.</param>
    /// <returns>This builder instance for method chaining.</returns>
    /// <exception cref="ArgumentException">Thrown if providerId is null or empty.</exception>
    public WindowDecorationBuilder WithMenu(string providerId, bool isCompact = false)
    {
        if (string.IsNullOrWhiteSpace(providerId))
        {
            throw new ArgumentException("Provider ID must be a non-empty string.", nameof(providerId));
        }

        this.menu = new MenuOptions { MenuProviderId = providerId, IsCompact = isCompact };
        return this;
    }

    /// <summary>
    /// Sets the backdrop kind as a window-specific override.
    /// </summary>
    /// <param name="backdrop">The backdrop material to apply to this window, overriding the window category default.</param>
    /// <returns>This builder instance for method chaining.</returns>
    public WindowDecorationBuilder WithBackdrop(BackdropKind backdrop)
    {
        this.backdrop = backdrop;
        return this;
    }

    /// <summary>
    /// Sets whether system title bar overlay is enabled.
    /// </summary>
    /// <param name="enabled">True to enable system title bar overlay.</param>
    /// <returns>This builder instance for method chaining.</returns>
    public WindowDecorationBuilder WithSystemTitleBarOverlay(bool enabled)
    {
        this.enableSystemTitleBarOverlay = enabled;
        return this;
    }

    /// <summary>
    /// Sets the title bar height.
    /// </summary>
    /// <param name="height">The title bar height in pixels.</param>
    /// <returns>This builder instance for method chaining.</returns>
    /// <exception cref="ArgumentOutOfRangeException">Thrown if height is not positive.</exception>
    public WindowDecorationBuilder WithTitleBarHeight(double height)
    {
        if (height <= 0)
        {
            throw new ArgumentOutOfRangeException(nameof(height), "Title bar height must be positive.");
        }

        this.titleBar = this.titleBar with { Height = height };
        return this;
    }

    /// <summary>
    /// Hides the maximize button.
    /// </summary>
    /// <returns>This builder instance for method chaining.</returns>
    public WindowDecorationBuilder NoMaximize()
    {
        this.buttons = this.buttons with { ShowMaximize = false };
        return this;
    }

    /// <summary>
    /// Hides the minimize button.
    /// </summary>
    /// <returns>This builder instance for method chaining.</returns>
    public WindowDecorationBuilder NoMinimize()
    {
        this.buttons = this.buttons with { ShowMinimize = false };
        return this;
    }

    /// <summary>
    /// Explicitly disables the backdrop effect for this window.
    /// </summary>
    /// <returns>This builder instance for method chaining.</returns>
    public WindowDecorationBuilder NoBackdrop()
    {
        this.backdrop = BackdropKind.None;
        return this;
    }

    /// <summary>
    /// Builds and validates the window decoration options.
    /// </summary>
    /// <returns>An immutable <see cref="WindowDecorationOptions"/> instance.</returns>
    /// <exception cref="ValidationException">
    /// Thrown if the configuration is invalid (e.g., chrome disabled with menu specified,
    /// primary window without close button).
    /// </exception>
    public WindowDecorationOptions Build()
    {
        var options = new WindowDecorationOptions
        {
            Category = this.category,
            ChromeEnabled = this.chromeEnabled,
            TitleBar = this.titleBar,
            Buttons = this.buttons,
            Menu = this.menu,
            Backdrop = this.backdrop,
            EnableSystemTitleBarOverlay = this.enableSystemTitleBarOverlay,
        };

        options.Validate();
        return options;
    }
}
