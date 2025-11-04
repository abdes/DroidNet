// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Controls.Menus;

namespace DroidNet.Aura.Decoration;

/// <summary>
///     Thread-safe menu provider implementation using a <see cref="MenuBuilder"/> factory function.
/// </summary>
/// <remarks>
///     This provider is suitable for menus that do not require dependency injection.
///     The factory function is invoked on each call to <see cref="CreateMenuSource"/> with
///     thread-safe locking to ensure concurrent window creation scenarios are handled correctly.
///     <para>
///     For menus that require services from the DI container, use <see cref="ScopedMenuProvider"/>
///     instead.</para>
/// </remarks>
/// <example>
///     <code><![CDATA[
///     // Simple static menu
///     var provider = new MenuProvider(
///         "App.MainMenu",
///         () => new MenuBuilder()
///             .AddMenuItem("File", null, null, null)
///             .AddSubmenu("Edit", builder =>
///             {
///                 builder.AddMenuItem("Cut", null, null, "Ctrl+X");
///                 builder.AddMenuItem("Copy", null, null, "Ctrl+C");
///                 builder.AddMenuItem("Paste", null, null, "Ctrl+V");
///             }));
///
///     // Register in DI
///     services.AddSingleton&lt;IMenuProvider&gt;(provider);
///     ]]></code>
/// </example>
/// <seealso cref="IMenuProvider"/>
/// <seealso cref="ScopedMenuProvider"/>
public sealed class MenuProvider : IMenuProvider
{
    private readonly Func<MenuBuilder> builderFactory;
    private readonly Lock lockObject = new();

    /// <summary>
    ///     Initializes a new instance of the <see cref="MenuProvider"/> class.
    /// </summary>
    /// <param name="providerId">
    ///     The unique identifier for this menu provider. Must be non-empty.
    /// </param>
    /// <param name="builderFactory">
    ///     A factory function that creates and configures a <see cref="MenuBuilder"/>.
    ///     This function is invoked each time <see cref="CreateMenuSource"/> is called.
    /// </param>
    /// <exception cref="ArgumentException">
    ///     Thrown if <paramref name="providerId"/> is null, empty, or whitespace.
    /// </exception>
    /// <exception cref="ArgumentNullException">
    ///     Thrown if <paramref name="builderFactory"/> is <see langword="null"/>.
    /// </exception>
    public MenuProvider(string providerId, Func<MenuBuilder> builderFactory)
    {
        if (string.IsNullOrWhiteSpace(providerId))
        {
            throw new ArgumentException("Provider ID must be a non-empty string.", nameof(providerId));
        }

        this.ProviderId = providerId;
        this.builderFactory = builderFactory ?? throw new ArgumentNullException(nameof(builderFactory));
    }

    /// <inheritdoc/>
    public string ProviderId { get; }

    /// <inheritdoc/>
    /// <remarks>
    ///     This method is thread-safe and can be called concurrently from multiple threads.
    ///     A lock is used to ensure that the <see cref="MenuBuilder"/> factory function
    ///     is executed serially to prevent potential data races.
    /// </remarks>
    public IMenuSource CreateMenuSource()
    {
        lock (this.lockObject)
        {
            var builder = this.builderFactory();
            return builder.Build();
        }
    }
}
