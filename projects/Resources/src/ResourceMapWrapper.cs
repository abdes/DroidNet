// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.Windows.ApplicationModel.Resources;

namespace DroidNet.Resources;

/// <summary>
/// Wraps the <see cref="ResourceMap"/> class to implement the <see cref="IResourceMap"/> interface.
/// </summary>
/// <remarks>
/// <param name="resourceMap">The <see cref="ResourceMap"/> instance to wrap.</param>
/// The <see cref="ResourceMapWrapper"/> class is introduced to provide an implementation of the
/// <see cref="IResourceMap"/> interface. This allows for easier unit testing by enabling the use of mock
/// or dummy implementations of resource maps. The challenge was to test the module without having a real
/// <see cref="ResourceMap"/> with real resources. By using this wrapper, we can create testable
/// implementations that simulate the behavior of <see cref="ResourceMap"/> without requiring actual resources.
/// </remarks>
internal sealed class ResourceMapWrapper(ResourceMap resourceMap) : IResourceMap
{
    /// <inheritdoc cref="ResourceMap.TryGetValue(string)" />
    public ResourceCandidate? TryGetValue(string key) => resourceMap.TryGetValue(key);

    /// <inheritdoc cref="ResourceMap.GetSubtree(string)" />
    public IResourceMap? GetSubtree(string key)
    {
        var subMap = resourceMap.GetSubtree(key);
        return subMap != null ? new ResourceMapWrapper(subMap) : null;
    }
}
