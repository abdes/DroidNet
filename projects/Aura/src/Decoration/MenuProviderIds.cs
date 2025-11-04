// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Aura.Decoration;

/// <summary>
///     Common menu provider identifiers for standard application menus.
/// </summary>
/// <remarks>
///     Use these constants when registering and referencing menu providers to ensure
///     consistency across your application.
/// </remarks>
public static class MenuProviderIds // TODO: remove this?
{
    /// <summary>
    ///     Identifier for the main application menu (typically for primary windows).
    /// </summary>
    public const string MainMenu = "App.MainMenu";

    /// <summary>
    ///     Identifier for context menus.
    /// </summary>
    public const string ContextMenu = "App.ContextMenu";

    /// <summary>
    ///     Identifier for tool window menus.
    /// </summary>
    public const string ToolMenu = "App.ToolMenu";
}
