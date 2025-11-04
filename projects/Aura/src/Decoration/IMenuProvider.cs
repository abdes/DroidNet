// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Controls.Menus;

namespace DroidNet.Aura.Decoration;

/// <summary>
///     Provides menu instances for windows to avoid shared mutable state.
/// </summary>
/// <remarks>
///     Menu providers are registered in the dependency injection container and resolved by their
///     <see cref="ProviderId"/> when a window is created. Each provider must create a new
///     <see cref="IMenuSource"/> instance per call to <see cref="CreateMenuSource"/> to ensure
///     that each window has its own menu state and observable collections.
///     <para>
///     Implementations must be thread-safe as <see cref="CreateMenuSource"/> may be called
///     concurrently during window creation.</para>
/// </remarks>
/// <example>
///     <code><![CDATA[
///     // Register a simple menu provider
///     services.AddMenuProvider(
///         "App.MainMenu",
///         () => new MenuBuilder()
///             .AddMenuItem("File", null, null, null)
///             .AddMenuItem("Edit", null, null, null));
///
///     // Register a scoped menu provider with DI dependencies
///     services.AddScopedMenuProvider(
///         "App.ToolMenu",
///         (builder, sp) =>
///         {
///             var commandService = sp.GetRequiredService&lt;ICommandService&gt;();
///             builder.AddMenuItem("Open", commandService.OpenCommand);
///         });
///     ]]></code>
/// </example>
/// <seealso cref="MenuProvider"/>
/// <seealso cref="ScopedMenuProvider"/>
public interface IMenuProvider
{
    /// <summary>
    ///     Gets the unique identifier for this menu provider.
    /// </summary>
    /// <value>
    ///     A non-empty string that uniquely identifies this provider within the application. This
    ///     identifier is used in <see cref="MenuOptions.MenuProviderId"/> to specify which provider
    ///     should create the menu for a window.
    /// </value>
    /// <remarks>
    ///     Provider IDs should follow a hierarchical naming convention such as "App.MainMenu",
    ///     "Tool.ContextMenu", or "Document.EditorMenu" to avoid collisions.
    /// </remarks>
    public string ProviderId { get; }

    /// <summary>
    ///     Creates a new menu source instance for a window.
    /// </summary>
    /// <returns>
    ///     A new <see cref="IMenuSource"/> instance with its own observable collection of menu
    ///     items. Each call must return a distinct instance to avoid shared mutable state between
    ///     windows.
    /// </returns>
    /// <remarks>
    ///     This method may be called concurrently during multi-window creation scenarios.
    ///     Implementations must ensure thread-safety through appropriate synchronization mechanisms
    ///     such as locks or by using thread-safe data structures.
    ///     <para>
    ///     The returned <see cref="IMenuSource"/> will be disposed when the window is closed.
    ///     Providers should not cache menu sources or share them between windows.</para>
    /// </remarks>
    /// <exception cref="InvalidOperationException">
    ///     Thrown if the provider cannot create a menu source due to missing dependencies or
    ///     invalid configuration.
    /// </exception>
    public IMenuSource CreateMenuSource();
}
