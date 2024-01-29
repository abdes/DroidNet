// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing.Contracts;

/// <summary>
/// Provides functionality to parse a routing URL string into a
/// <see cref="UrlTree" />.
/// </summary>
public interface IUrlParser
{
    /// <summary>Parses the URL string into a <see cref="UrlTree" />.</summary>
    /// <param name="url">URL string to parse.</param>
    /// <returns>
    /// A <see cref="UrlTree" /> representing the hierarchy of route segments
    /// and segment groups.
    /// </returns>
    /// <exception cref="UriFormatException">The URL string is malformed.</exception>
    UrlTree Parse(string url);
}
