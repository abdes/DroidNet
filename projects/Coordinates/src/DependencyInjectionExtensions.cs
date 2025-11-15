// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DryIoc;
using Microsoft.UI.Xaml;

namespace DroidNet.Coordinates;

/// <summary>
///     Provides helper methods to register spatial mapping services with DryIoc containers.
/// </summary>
/// <remarks>
///    The preferred and intuitive way to obtain spatial mappers is to inject the delegates <see
///    cref="SpatialMapperFactory"/> or <see cref="RawSpatialMapperFactory"/>, which will in turn
///    call the factory. This keeps DryIoc happy with no ambiguity from defaulted constructor
///    arguments, while providing a clear API and still allowing future dependency injection into
///    the factory implementation.
/// </remarks>
public static class DependencyInjectionExtensions
{
    /// <summary>
    ///     Registers spatial mapping services and returns the container for fluent chaining.
    /// </summary>
    /// <param name="container">The DryIoc container to configure.</param>
    /// <returns>The same container instance for chaining registrations.</returns>
    public static IContainer WithSpatialMapping(this IContainer container)
    {
        ArgumentNullException.ThrowIfNull(container);

        // Register the factory implementation, transient to avoid any issue if the factory later
        // becomes stateless and depends on scoped services.
        container.Register<SpatialMapperFactoryImpl>(reuse: Reuse.Transient);

        // This delegate wrapper supports creating mappers using Window/Element context. Both are
        // optional.
        container.RegisterDelegate<SpatialMapperFactory>(r =>
        {
            var f = r.Resolve<SpatialMapperFactoryImpl>();
            return (window, element) => f.Create(window, element);
        });

        // This delegate wrapper supports creating mappers using raw HWND.
        container.RegisterDelegate<RawSpatialMapperFactory>(r =>
        {
            var f = r.Resolve<SpatialMapperFactoryImpl>();
            return (hwnd) => f.CreateFromHwnd(hwnd);
        });

        return container;
    }

    /// <summary>
    ///    Internal implementation of the spatial mapper factory, with option to inject dependencies.
    /// </summary>
    [System.Diagnostics.CodeAnalysis.SuppressMessage("Performance", "CA1822:Mark members as static", Justification = "Future dependency injection.")]
    internal sealed class SpatialMapperFactoryImpl(/* add deps here, e.g. ILogger logger */)
    {
        /// <summary>
        ///     Creates a mapper using a Window/Element context. Additional dependencies injected in
        ///     the factory may be passed to the constructor of the SpatialMapper here.
        /// </summary>
        /// <param name="window">The window context for the spatial mapper, or null.</param>
        /// <param name="element">The framework element context for the spatial mapper, or null.</param>
        /// <returns>The created spatial mapper instance.</returns>
        public ISpatialMapper Create(Window? window = null, FrameworkElement? element = null)
            => new SpatialMapper(window, element);

        /// <summary>
        ///     Creates a spatial mapper using a window handle (HWND).
        /// </summary>
        /// <param name="hwnd">
        ///     The window handle to use for the spatial mapper. Optional and defaults to <see
        ///     cref="IntPtr.Zero"/>.
        /// </param>
        /// <returns>The created spatial mapper instance.</returns>
        public ISpatialMapper CreateFromHwnd(IntPtr hwnd = default)
            => new SpatialMapper(hwnd);
    }
}
