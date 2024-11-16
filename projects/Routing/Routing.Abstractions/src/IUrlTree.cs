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

    /// <summary>Gets a value indicating whether this tree represents a relative URL.</summary>
    /// <value><see langword="true" /> if the tree was creating from a relative URLm; <see langword="false" /> otherwise.</value>
    bool IsRelative { get; }
}
