// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.UI.Xaml;

namespace DroidNet.Aura.Windowing;

/// <summary>
///     Defines a factory for creating window instances via dependency injection.
/// </summary>
/// <remarks>
///     <para>
///     This factory abstracts window creation by delegating to a dependency injection container. Implementations
///     resolve <see cref="Window"/> instances from the DI container, which enables sophisticated composition scenarios,
///     flexible registration strategies, and testability.</para>
///     <para>
///     <b>Registration Requirements:</b><br/> All window types must be registered in the DI container. Supported
///     registration patterns include specific window types, base type registrations, and keyed registrations for
///     targeted window instances or type variants. Keyed registrations enable decoupling: call sites reference a
///     logical key rather than concrete types, allowing window implementations to be swapped or configured at the
///     container level.</para>
///     <para>
///     <b>Decorated Window Creation:</b><br/> The <see cref="CreateDecoratedWindow{TWindow}"/> method creates windows
///     with automatic decoration based on the provided <see cref="WindowCategory"/>. This integrates with the Aura
///     configuration system to apply category-specific visual and behavioral settings. Decoration is applied during
///     window creation and is separate from the base window instance.</para>
///     <para>
///     <b>Return Type:</b><br/> All methods return XAML-based <see cref="Window"/> instances compatible with WinUI 3.
///     The underlying <see cref="Window.AppWindow"/> property provides access to platform-level window management
///     features such as presenter configuration, window placement, and custom chrome behavior.</para>
/// </remarks>
/// <seealso cref="WindowCategory"/>
/// <seealso cref="WindowContext"/>
/// <seealso cref="IWindowManagerService"/>
public interface IWindowFactory
{
    /// <summary>
    ///     Creates a window of the specified type using dependency injection.
    /// </summary>
    /// <typeparam name="TWindow">
    ///     The window type to create. Must be registered in the dependency injection container.
    /// </typeparam>
    /// <param name="metadata">
    ///     Optional metadata to associate with the window. Will be available in the managed <see cref="WindowContext"/>.
    /// </param>
    /// <returns>
    ///     An instance of <typeparamref name="TWindow"/> with constructor dependencies resolved from the DI container.
    /// </returns>
    /// <remarks>
    ///     This generic overload provides compile-time type safety and requires an explicit registration for
    ///     <typeparamref name="TWindow"/> in the container. Use this when the window type is known at the call site.
    /// </remarks>
    public Task<TWindow> CreateWindow<TWindow>(IReadOnlyDictionary<string, object>? metadata = null)
        where TWindow : Window;

    /// <summary>
    ///     Creates a window using a keyed DI registration.
    /// </summary>
    /// <param name="key">
    ///     The key identifying a keyed <see cref="Window"/> registration in the dependency injection container.
    /// </param>
    /// <param name="metadata">
    ///     Optional metadata to associate with the window. Will be available in the managed <see cref="WindowContext"/>.
    /// </param>
    /// <returns>
    ///     A <see cref="Window"/> instance resolved from the container using the provided key.
    /// </returns>
    /// <remarks>
    ///     This overload enables runtime window selection through keyed registrations, allowing multiple <see
    ///     cref="Window"/> implementations to be registered and distinguished by key. Use this when the specific window
    ///     implementation is determined by configuration, user input, or other runtime conditions.
    /// </remarks>
    public Task<Window> CreateWindow(string key, IReadOnlyDictionary<string, object>? metadata = null);

    /// <summary>
    ///     Creates a window of the specified type with automatic decoration for the given category.
    /// </summary>
    /// <typeparam name="TWindow">
    ///     The window type to create. Must be registered in the dependency injection container.
    /// </typeparam>
    /// <param name="category">
    ///     The <see cref="WindowCategory"/> that determines decoration applied to the created window. Decoration
    ///     settings are resolved from the Aura configuration for the specified category.
    /// </param>
    /// <param name="metadata">
    ///     Optional metadata to associate with the window. Will be available in the managed <see cref="WindowContext"/>.
    /// </param>
    /// <returns>
    ///     A new instance of <typeparamref name="TWindow"/> with dependencies resolved and category-specific decoration
    ///     applied.
    /// </returns>
    /// <remarks>
    ///     <para>
    ///     This method combines window creation with automatic decoration. The decoration process applies
    ///     category-specific configuration such as backdrop effects, window chrome settings, and visual properties
    ///     based on the category's configuration entry in the Aura settings.</para>
    ///     <para>
    ///     Decoration is applied after the window is fully created and initialized, ensuring all constructor-injected
    ///     dependencies and property bindings are established before decoration logic runs.</para>
    /// </remarks>
    public Task<TWindow> CreateDecoratedWindow<TWindow>(WindowCategory category, IReadOnlyDictionary<string, object>? metadata = null)
        where TWindow : Window;
}
