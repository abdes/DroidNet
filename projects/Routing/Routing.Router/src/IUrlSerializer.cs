// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing;

/// <summary>
/// Provides functionality to serialize a route <see cref="UrlTree"/> to a string and to deserialize a URL string into a <see cref="UrlTree"/>.
/// </summary>
/// <remarks>
/// The <see cref="IUrlSerializer"/> interface defines methods for converting URL strings into structured <see cref="IUrlTree"/> objects and vice versa.
/// This is essential for managing the routing state within the application, allowing URLs to be parsed into navigable trees and serialized back into strings for navigation.
/// </remarks>
public interface IUrlSerializer
{
    /// <summary>
    /// Parses the URL string into a <see cref="UrlTree"/>.
    /// </summary>
    /// <param name="url">The URL string to parse.</param>
    /// <returns>A <see cref="IUrlTree"/> representing the hierarchy of route segments and segment groups.</returns>
    /// <exception cref="UriFormatException">
    /// Thrown if the URL string is malformed.
    /// </exception>
    /// <remarks>
    /// This method takes a URL string and converts it into a structured <see cref="IUrlTree"/> object.
    /// It processes the URL's path segments, query parameters, and any special routing syntax to build a navigable tree structure.
    /// </remarks>
    /// <example>
    /// <strong>Example Usage</strong>
    /// <code><![CDATA[
    /// var serializer = new DefaultUrlSerializer();
    /// var urlTree = serializer.Parse("/inbox/33(popup:compose)?open=true");
    /// ]]></code>
    /// </example>
    public IUrlTree Parse(string url);

    /// <summary>
    /// Converts the provided URL <paramref name="tree"/> into a string.
    /// </summary>
    /// <param name="tree">The URL tree to convert.</param>
    /// <returns>A <see cref="string"/> representation of the URL tree.</returns>
    /// <remarks>
    /// This method takes a structured <see cref="IUrlTree"/> object and converts it back into a URL string.
    /// It serializes the tree's path segments, query parameters, and any special routing syntax to produce a navigable URL.
    /// </remarks>
    /// <example>
    /// <strong>Example Usage</strong>
    /// <code><![CDATA[
    /// var serializer = new DefaultUrlSerializer();
    /// var urlTree = new UrlTree(...); // Assume this is a populated URL tree
    /// var urlString = serializer.Serialize(urlTree);
    /// ]]></code>
    /// </example>
    public string Serialize(IUrlTree tree);
}
