// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing.Contracts;

using System;

/// <summary>
/// Provides functionality to serialize a route <see cref="UrlTree" /> to a
/// string and to deserialize a URL string into a <see cref="UrlTree" />.
/// </summary>
public interface IUrlSerializer
{
    /// <summary>Parses the URL string into a <see cref="UrlTree" />.</summary>
    /// <param name="url">URL string to parse.</param>
    /// <returns>
    /// A <see cref="UrlTree" /> representing the hierarchy of route segments
    /// and segment groups.
    /// </returns>
    /// <exception cref="UriFormatException">
    /// The URL string is malformed.
    /// </exception>
    UrlTree Parse(string url);

    /// <summary>
    /// Converts the provided URL <paramref name="tree" /> into a string.
    /// </summary>
    /// <param name="tree">The URl tree to convert.</param>
    /// <returns>
    /// A <see cref="string" /> representation of the URL tree.
    /// </returns>
    string Serialize(UrlTree tree);
}
