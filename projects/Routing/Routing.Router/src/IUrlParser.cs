// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing;

/// <summary>
/// Provides functionality to parse a navigation URL string into a URL tree.
/// </summary>
public interface IUrlParser
{
    /// <summary>
    /// Parses the URL string into a <see cref="IUrlTree" />.
    /// </summary>
    /// <param name="url">URL string to parse.</param>
    /// <returns>A <see cref="IUrlTree" /> representing the hierarchy of route segments and segment groups.
    /// </returns>
    /// <exception cref="UriFormatException">The URL string is malformed.</exception>
    public IUrlTree Parse(string url);
}
