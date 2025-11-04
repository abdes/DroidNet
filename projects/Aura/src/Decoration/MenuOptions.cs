// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json.Serialization;
using DroidNet.Aura.Decoration.Serialization;

namespace DroidNet.Aura.Decoration;

/// <summary>
///     Immutable configuration options for a window's menu bar.
/// </summary>
/// <remarks>
///     Menu options specify which menu provider should be used to create the menu bar for a window.
///     The menu provider is resolved from the dependency injection container using the <see
///     cref="MenuProviderId"/>.
///     <para>
///     Menu options are serialized to JSON using a custom converter that stores only the provider
///     ID and compact mode setting. The actual menu provider is resolved at runtime from the DI
///     container.</para>
///     <para>
///     Use <see langword="null"/> for the Menu property in WindowDecorationOptions to indicate that
///     no menu should be displayed.</para>
/// </remarks>
/// <example>
///     <code><![CDATA[
///     // Standard application menu
///     var menuOptions = new MenuOptions
///     {
///         MenuProviderId = "App.MainMenu",
///     };
///
///     // Compact menu for tool windows
///     var compactMenu = new MenuOptions
///     {
///         MenuProviderId = "App.ToolMenu",
///         IsCompact = true,
///     };
///     ]]></code>
/// </example>
[JsonConverter(typeof(MenuOptionsJsonConverter))]
public sealed record MenuOptions
{
    /// <summary>
    ///     Gets the identifier of the menu provider to use for creating the menu source.
    /// </summary>
    /// <value>
    ///     A non-empty string identifying a registered menu provider in the DI container.
    /// </value>
    /// <remarks>
    ///     The provider ID is used to resolve a menu provider from the dependency injection
    ///     container. If the provider is not found, a warning is logged and the window is created
    ///     without a menu (graceful degradation).
    /// </remarks>
    public required string MenuProviderId { get; init; }

    /// <summary>
    ///     Gets a value indicating whether the menu should be displayed in compact mode.
    /// </summary>
    /// <value>
    ///     <see langword="true"/> for compact menu rendering; otherwise, <see langword="false"/>.
    ///     Default is <see langword="false"/>.
    /// </value>
    /// <remarks>
    ///     Compact mode is useful for tool windows or secondary windows where vertical space is
    ///     limited. The exact visual appearance of compact mode depends on the menu implementation.
    /// </remarks>
    public bool IsCompact { get; init; }
}
