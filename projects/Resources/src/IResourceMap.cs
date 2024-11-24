// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.Windows.ApplicationModel.Resources;

namespace DroidNet.Resources;

/// <summary>
/// Defines an interface for resource maps to facilitate testing and abstraction.
/// </summary>
/// <remarks>
/// The <see cref="IResourceMap"/> interface is introduced to abstract the <see cref="ResourceMap"/> class
/// from the Windows App SDK. This abstraction allows for easier unit testing by enabling the use of mock
/// or dummy implementations of resource maps. The challenge was to test the module without having a real
/// <see cref="ResourceMap"/> with real resources. By using this interface, we can create testable
/// implementations that simulate the behavior of <see cref="ResourceMap"/> without requiring actual resources.
/// </remarks>
public interface IResourceMap
{
    /// <inheritdoc cref="ResourceMap.TryGetValue(string)" />
    public ResourceCandidate? TryGetValue(string key);

    /// <inheritdoc cref="ResourceMap.GetSubtree(string)" />
    public IResourceMap? GetSubtree(string key);
}
