// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System;
using DryIoc;
using Microsoft.UI.Xaml;

namespace DroidNet.Coordinates;

/// <summary>
///     Provides helper methods to register spatial mapping services with DryIoc containers.
/// </summary>
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

        container.Register<ISpatialMapper, SpatialMapper>(
            reuse: Reuse.Transient,
            made: Made.Of(() => new SpatialMapper(Arg.Of<FrameworkElement>(), Arg.Of<Window?>(IfUnresolved.ReturnDefault))));

        container.RegisterDelegate<SpatialMapperFactory>(
            static r =>
            {
                var factory = r.Resolve<Func<FrameworkElement, Window?, ISpatialMapper>>();

                return (FrameworkElement element, Window? window) =>
                {
                    ArgumentNullException.ThrowIfNull(element);
                    return factory(element, window);
                };
            });

        return container;
    }
}
