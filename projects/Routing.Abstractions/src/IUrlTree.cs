// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing;

/// <summary>Represents a parsed navigation URL.</summary>
public interface IUrlTree
{
    /// <summary>
    /// Gets the root <see cref="IUrlSegmentGroup" /> of the parsed URL tree.
    /// </summary>
    IUrlSegmentGroup Root { get; }

    /// <summary>Gets the query parameters in this URL tree.</summary>
    IParameters QueryParams { get; }

    /// <summary>
    /// Gets a value indicating whether this tree represents a relative URL
    /// (i.e. starts with a double dot path).
    /// </summary>
    /// <value>
    /// <c>true</c> if the first segment of the first child of the tree root is
    /// a double dot. <c>false</c> otherwise.
    /// </value>
    bool IsRelative { get; }
}
