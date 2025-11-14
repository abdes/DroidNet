// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.Windows.ApplicationModel.Resources;

namespace DroidNet.Resources;

/// <summary>
///     Wraps the Windows App SDK <see cref="Microsoft.Windows.ApplicationModel.Resources.ResourceMap"/> and exposes a typed,
///     SDK-agnostic API.
/// </summary>
/// <remarks>
///     Implements <see cref="IResourceMap"/> by delegating lookups and subtree
///     operations to an underlying <see cref="Microsoft.Windows.ApplicationModel.Resources.ResourceMap"/> instance.
/// </remarks>
internal sealed class ResourceMapWrapper : IResourceMap
{
    /// <summary>
    ///     Initializes a new instance of the <see cref="ResourceMapWrapper"/> class.
    /// </summary>
    /// <param name="resourceMap">The underlying <see cref="Microsoft.Windows.ApplicationModel.Resources.ResourceMap"/> to wrap.</param>
    /// <exception cref="ArgumentNullException">
    ///     Thrown when <paramref name="resourceMap"/> is <see langword="null"/>.
    /// </exception>
    public ResourceMapWrapper(ResourceMap resourceMap)
    {
        ArgumentNullException.ThrowIfNull(resourceMap);
        this.ResourceMap = resourceMap;
    }

    /// <summary>
    ///     Gets the underlying Windows App SDK <see cref="Microsoft.Windows.ApplicationModel.Resources.ResourceMap"/> being wrapped.
    /// </summary>
    internal ResourceMap ResourceMap { get; }

    /// <summary>
    ///     Attempts to get the localized value for the specified resource <paramref name="key"/>.
    /// </summary>
    /// <param name="key">The resource key to look up.</param>
    /// <param name="value">
    ///     When this method returns, contains the localized value if the key was found;
    ///     otherwise <see langword="null"/>.
    /// </param>
    /// <returns><see langword="true"/> if the value was found; otherwise <see langword="false"/>.</returns>
    public bool TryGetValue(string key, out string? value)
    {
        var candidate = this.ResourceMap.TryGetValue(key);
        value = candidate?.ValueAsString;
        return candidate is not null;
    }

    /// <summary>
    ///     Returns a subtree for the specified resource <paramref name="key"/>, or
    ///     <see cref="EmptyResourceMap"/> if the subtree is not found.
    /// </summary>
    /// <param name="key">The resource key that identifies the subtree.</param>
    /// <returns>
    ///     A new <see cref="ResourceMapWrapper"/> that wraps the subtree <see cref="Microsoft.Windows.ApplicationModel.Resources.ResourceMap"/>,
    ///     or <see cref="EmptyResourceMap"/> if no subtree exists for the given key.
    /// </returns>
    [System.Diagnostics.CodeAnalysis.SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "we use EmptyResourceMap on error")]
    public IResourceMap GetSubtree(string key)
    {
        try
        {
            var subtree = this.ResourceMap.GetSubtree(key);
            return subtree is not null ? new ResourceMapWrapper(subtree) : new EmptyResourceMap();
        }
        catch
        {
            return new EmptyResourceMap();
        }
    }
}
