// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Resources;

/// <summary>
///     Represents an <see cref="IResourceMap"/> that contains no resources and
///     always reports missing keys.
/// </summary>
internal sealed class EmptyResourceMap : IResourceMap
{
    /// <inheritdoc />
    public bool TryGetValue(string key, out string? value)
    {
        value = null;
        return false;
    }

    /// <inheritdoc />
    public IResourceMap GetSubtree(string key) => this;
}
