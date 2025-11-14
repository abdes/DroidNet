// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Resources;

/// <summary>
///     Minimal, SDK-agnostic resource map abstraction used by DroidNet.Resources.
/// </summary>
public interface IResourceMap
{
    /// <summary>
    ///     Attempts to get the localized value for <paramref name="key"/>.
    /// </summary>
    /// <param name="key">The resource key to lookup.</param>
    /// <param name="value">The localized value when present.</param>
    /// <returns>True if the value was found; otherwise false.</returns>
    public bool TryGetValue(string key, out string? value);

    /// <summary>
    ///     Returns a subtree for <paramref name="key"/> or <see cref="EmptyResourceMap"/> if not found.
    /// </summary>
    /// <param name="key">The resource key to lookup for a subtree.</param>
    /// <returns>The subtree <see cref="IResourceMap"/> for the given key, or <see cref="EmptyResourceMap"/> if not found.</returns>
    public IResourceMap GetSubtree(string key);
}
