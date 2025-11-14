// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Reflection;

namespace DroidNet.Resources;

/// <summary>
///     Default implementation of <see cref="IResourceMapProvider"/> that delegates to
///     <see cref="ResourceMapProvider"/>.
/// </summary>
/// <remarks>
///     This implementation wraps the static <see cref="ResourceMapProvider"/> methods,
///     providing a dependency-injectable interface while maintaining the existing caching behavior.
/// </remarks>
public sealed class DefaultResourceMapProvider : IResourceMapProvider
{
    /// <inheritdoc/>
    public IResourceMap ApplicationResourceMap => ResourceMapProvider.ApplicationResourceMap;

    /// <inheritdoc/>
    public IResourceMap GetAssemblyResourceMap(Assembly assembly)
        => ResourceMapProvider.GetAssemblyResourceMap(assembly);
}
