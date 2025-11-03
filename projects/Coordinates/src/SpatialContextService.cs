// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.UI.Xaml;

namespace DroidNet.Coordinates;

/// <summary>
///     Service for providing spatial mappers via dependency injection.
/// </summary>
/// <param name="factory">The factory for creating spatial mappers.</param>
public class SpatialContextService(SpatialMapperFactory factory)
{
    private readonly SpatialMapperFactory factory = factory;

    /// <summary>
    ///     Gets a spatial mapper for the specified element and window.
    /// </summary>
    /// <param name="element">The framework element.</param>
    /// <param name="window">The window.</param>
    /// <returns>The spatial mapper.</returns>
    public ISpatialMapper GetMapper(FrameworkElement element, Window window)
        => this.factory(element, window);

    /// <summary>
    ///     Gets a lazy spatial mapper for the specified element and window.
    /// </summary>
    /// <param name="element">The framework element.</param>
    /// <param name="window">The window.</param>
    /// <returns>The lazy spatial mapper.</returns>
    public Lazy<ISpatialMapper> GetLazyMapper(FrameworkElement element, Window window)
        => new(() => this.factory(element, window));
}
