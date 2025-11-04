// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using DroidNet.Aura.Decoration;
using DroidNet.Controls.Menus;
using DryIoc;
using Microsoft.Extensions.Logging;
using Microsoft.UI.Xaml.Controls;

namespace DroidNet.Samples.Aura.MultiWindow;

/// <summary>
/// Configures and registers menu providers for the multi-window sample application.
/// </summary>
/// <remarks>
/// This class demonstrates best practices for menu registration in Aura applications:
/// <list type="bullet">
/// <item>Menus are registered separately from WithAura() for better organization</item>
/// <item>Menu providers are registered as singletons implementing IMenuProvider</item>
/// <item>Menu building logic is separated from DI registration</item>
/// <item>Menus are referenced by ID from window decorations</item>
/// </list>
/// </remarks>
[ExcludeFromCodeCoverage]
internal static class MenuConfiguration
{
    /// <summary>
    /// Main application menu displayed in the title bar.
    /// </summary>
    public const string MainMenuId = "App.MainMenu";

    /// <summary>
    /// Registers all menu providers with the DryIoc container.
    /// </summary>
    /// <param name="container">The DryIoc container to configure.</param>
    /// <returns>The container for chaining.</returns>
    public static IContainer RegisterMenus(this IContainer container)
    {
        // Register main application menu with ILoggerFactory injection
        container.RegisterDelegate<IMenuProvider>(
            resolver =>
            {
                var loggerFactory = resolver.Resolve<ILoggerFactory>();
                return new MenuProvider(MainMenuId, () => BuildMainMenu(loggerFactory));
            },
            Reuse.Singleton,
            serviceKey: MainMenuId);

        return container;
    }

    /// <summary>
    /// Builds the main application menu with file, window, view, and help sections.
    /// </summary>
    /// <param name="loggerFactory">The logger factory for menu services.</param>
    /// <returns>A configured menu builder.</returns>
    private static MenuBuilder BuildMainMenu(ILoggerFactory loggerFactory)
    {
        var builder = new MenuBuilder(loggerFactory);

        BuildFileMenu(builder);
        BuildWindowMenu_ForMain(builder);
        BuildViewMenu(builder);
        BuildHelpMenu(builder);

        return builder;
    }

    /// <summary>
    /// Builds the File menu section.
    /// </summary>
    private static void BuildFileMenu(MenuBuilder builder)
        => _ = builder.AddSubmenu(
            "File",
            fileMenu =>
            {
                _ = fileMenu.AddMenuItem(
                    "New Document Window",
                    icon: new FontIconSource { Glyph = "\uE8A5", FontSize = 16 },
                    acceleratorText: "Ctrl+N");

                _ = fileMenu.AddMenuItem(
                    "New Tool Window",
                    icon: new FontIconSource { Glyph = "\uE8A7", FontSize = 16 });

                _ = fileMenu.AddSeparator();

                _ = fileMenu.AddMenuItem(
                    "Exit",
                    icon: new FontIconSource { Glyph = "\uE8BB", FontSize = 16 },
                    acceleratorText: "Alt+F4");
            });

    /// <summary>
    /// Builds the Window menu section for the main menu.
    /// </summary>
    private static void BuildWindowMenu_ForMain(MenuBuilder builder)
        => _ = builder.AddSubmenu(
            "Window",
            windowMenu =>
            {
                _ = windowMenu.AddMenuItem(
                    "Close All Windows",
                    icon: new FontIconSource { Glyph = "\uE8BB", FontSize = 16 });

                _ = windowMenu.AddSeparator();

                _ = windowMenu.AddSubmenu(
                    "Backdrop Style",
                    backdropMenu =>
                    {
                        _ = backdropMenu.AddMenuItem(
                            "None",
                            icon: new FontIconSource { Glyph = "\uE10A", FontSize = 16 });

                        _ = backdropMenu.AddMenuItem(
                            "Mica",
                            icon: new FontIconSource { Glyph = "\uE91B", FontSize = 16 });

                        _ = backdropMenu.AddMenuItem(
                            "Mica Alt",
                            icon: new FontIconSource { Glyph = "\uE91B", FontSize = 16 });

                        _ = backdropMenu.AddMenuItem(
                            "Acrylic",
                            icon: new FontIconSource { Glyph = "\uE91B", FontSize = 16 });
                    },
                    new FontIconSource { Glyph = "\uE790", FontSize = 16 });

                _ = windowMenu.AddSeparator();

                _ = windowMenu.AddMenuItem(
                    "Cascade",
                    icon: new FontIconSource { Glyph = "\uE8FD", FontSize = 16 });

                _ = windowMenu.AddMenuItem(
                    "Tile Horizontally",
                    icon: new FontIconSource { Glyph = "\uE89A", FontSize = 16 });

                _ = windowMenu.AddMenuItem(
                    "Tile Vertically",
                    icon: new FontIconSource { Glyph = "\uE89A", FontSize = 16 });
            });

    /// <summary>
    /// Builds the View menu section.
    /// </summary>
    private static void BuildViewMenu(MenuBuilder builder)
        => _ = builder.AddSubmenu(
            "View",
            viewMenu =>
            {
                _ = viewMenu.AddCheckableMenuItem(
                    "Show Toolbar",
                    isChecked: true,
                    icon: new FontIconSource { Glyph = "\uE8B9", FontSize = 16 });

                _ = viewMenu.AddCheckableMenuItem(
                    "Show Status Bar",
                    isChecked: true);

                _ = viewMenu.AddSeparator();

                _ = viewMenu.AddMenuItem(
                    "Refresh",
                    icon: new FontIconSource { Glyph = "\uE72C", FontSize = 16 },
                    acceleratorText: "F5");

                _ = viewMenu.AddMenuItem(
                    "Full Screen",
                    icon: new FontIconSource { Glyph = "\uE740", FontSize = 16 },
                    acceleratorText: "F11");
            });

    /// <summary>
    /// Builds the Help menu section.
    /// </summary>
    private static void BuildHelpMenu(MenuBuilder builder)
        => _ = builder.AddSubmenu(
            "Help",
            helpMenu =>
            {
                _ = helpMenu.AddMenuItem(
                    "Documentation",
                    icon: new FontIconSource { Glyph = "\uE897", FontSize = 16 },
                    acceleratorText: "F1");

                _ = helpMenu.AddSeparator();

                _ = helpMenu.AddMenuItem(
                    "About",
                    icon: new FontIconSource { Glyph = "\uE946", FontSize = 16 });
            });
}
