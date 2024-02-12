// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing;

using System.Collections.ObjectModel;

public interface IUrlSegmentGroup
{
    /// <summary>Gets the children of this segment group.</summary>
    /// <value>A read-only dictionary of this segment group's children.</value>
    IReadOnlyDictionary<OutletName, IUrlSegmentGroup> Children { get; }

    /// <summary>Gets the segments of this segment group.</summary>
    /// <value>A read-only collection of this segment group's segments.</value>
    ReadOnlyCollection<IUrlSegment> Segments { get; }

    /// <summary>
    /// Gets the children of this segment group with the child corresponding to
    /// the primary outlet first.
    /// </summary>
    /// <value>
    /// A read-only collection of this segment group's children, where the child
    /// corresponding to the primary outlet comes first.
    /// </value>
    ReadOnlyCollection<KeyValuePair<OutletName, IUrlSegmentGroup>> SortedChildren { get; }

    /// <summary>Gets the parent segment group of this segment group.</summary>
    /// <remarks>
    /// The parent is automatically set when this segment group is added as a
    /// child to another segment group.
    /// </remarks>
    /// <value>The parent segment group.</value>
    IUrlSegmentGroup? Parent { get; }

    /// <summary>
    /// Gets a value indicating whether this segment group is relative (i.e.
    /// starts with a double dot path).
    /// </summary>
    /// <value>
    /// <c>true</c> if the first segment in the group in a double dot.
    /// <c>false</c> otherwise.
    /// </value>
    bool IsRelative { get; }
}
