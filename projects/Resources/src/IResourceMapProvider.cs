// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Reflection;

namespace DroidNet.Resources;

/// <summary>
///     Provides access to application and assembly-specific resource maps.
/// </summary>
/// <remarks>
///     This interface enables dependency injection of resource map providers,
///     allowing custom implementations for testing or alternative resource systems.
/// </remarks>
public interface IResourceMapProvider
{
    /// <summary>
    ///     Gets the application's main resource map.
    /// </summary>
    public IResourceMap ApplicationResourceMap { get; }

    /// <summary>
    ///     Gets the resource map for a specific assembly.
    /// </summary>
    /// <param name="assembly">The assembly to get the resource map for.</param>
    /// <returns>The resource map for the assembly, or <see cref="EmptyResourceMap"/> if not found.</returns>
    public IResourceMap GetAssemblyResourceMap(Assembly assembly);
}
