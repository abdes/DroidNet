// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.ObjectModel;
using System.Diagnostics;
using Destructurama.Attributed;

namespace DroidNet.Routing;

/// <summary>
/// Represents a parsed sequence of URL segments, forming a path.
/// </summary>
public class UrlSegmentGroup : IUrlSegmentGroup
{
    private readonly Dictionary<OutletName, IUrlSegmentGroup> children = [];

    private readonly List<IUrlSegment> segments = [];

    /// <summary>
    /// Initializes a new instance of the <see cref="UrlSegmentGroup" /> class.
    /// </summary>
    /// <param name="segments">The URL segments.</param>
    /// <param name="children">The children of this <see cref="UrlSegmentGroup" />.</param>
    public UrlSegmentGroup(
        IEnumerable<IUrlSegment> segments,
        IDictionary<OutletName, IUrlSegmentGroup>? children = null)
    {
        this.segments.AddRange(segments);

        if (children == null || children.Count == 0)
        {
            return;
        }

        foreach (var pair in children)
        {
            this.AddChild(pair.Key, pair.Value);
        }
    }

    /// <summary>
    /// Gets the children of this <see cref="UrlSegmentGroup" />.
    /// </summary>
    public IReadOnlyDictionary<OutletName, IUrlSegmentGroup> Children => this.children.AsReadOnly();

    /// <summary>
    /// Gets the children of this <see cref="UrlSegmentGroup" /> with the child corresponding to the
    /// primary outlet first.
    /// </summary>
    /// <value>
    /// A read-only collection of this <see cref="UrlSegmentGroup" />'s children, where the child
    /// corresponding to the primary outlet comes first.
    /// </value>
    [NotLogged]
    public ReadOnlyCollection<KeyValuePair<OutletName, IUrlSegmentGroup>> SortedChildren
    {
        get
        {
            IUrlSegmentGroup? primary = null;
            if (this.children.TryGetValue(OutletName.Primary, out var value))
            {
                primary = value;
            }

            var res = this.children.Where(c => c.Key.IsNotPrimary).ToList();
            if (primary != null)
            {
                res.Insert(0, new KeyValuePair<OutletName, IUrlSegmentGroup>(OutletName.Primary, primary));
            }

            return res.AsReadOnly();
        }
    }

    /// <summary>
    /// Gets the parent <see cref="UrlSegmentGroup" /> of this <see cref="UrlSegmentGroup" />.
    /// </summary>
    /// <remarks>
    /// The parent is automatically set when this <seealso cref="UrlSegmentGroup" /> is added as a
    /// child to another <see cref="UrlSegmentGroup" />.
    /// </remarks>
    [NotLogged]
    public IUrlSegmentGroup? Parent { get; private set; }

    /// <summary>
    /// Gets the segments of this <see cref="UrlSegmentGroup" />.
    /// </summary>
    /// <value>
    /// A read-only collection of this <see cref="UrlSegmentGroup" />'s segments.
    /// </value>
    public ReadOnlyCollection<IUrlSegment> Segments => this.segments.AsReadOnly();

    /// <summary>
    /// Adds a child <see cref="UrlSegmentGroup" /> to the current <see cref="UrlSegmentGroup" />.
    /// </summary>
    /// <param name="outlet">The outlet name.</param>
    /// <param name="child">The child <see cref="UrlSegmentGroup" />.</param>
    public void AddChild(string outlet, IUrlSegmentGroup child)
    {
        this.children.Add(outlet, child);
        ((UrlSegmentGroup)child).Parent = this;
    }

    /// <summary>
    /// Remove the child <see cref="UrlSegmentGroup" /> with the specified outlet from the segment group.
    /// </summary>
    /// <param name="outlet">The outlet name of the child to remove.</param>
    /// <returns>
    /// <see langword="true" /> if the element is successfully removed; otherwise, <see langword="false" />.
    /// This method also returns <see langword="false" /> if <paramref name="outlet" /> was not found among
    /// the children.
    /// </returns>
    public bool RemoveChild(string outlet) => this.children.Remove(outlet);

    /// <summary>
    /// Serializes the <see cref="UrlSegmentGroup" /> into a string, making sure that its outlet
    /// name and segments are percent-encoded as required. required.
    /// </summary>
    /// <returns>
    /// A string representing the serialized form of the <see cref="UrlSegmentGroup" /> .
    /// </returns>
    public override string ToString() => this.segments.Count == 0 ? this.SerializeAsRoot() : this.Serialize();

    /// <summary>
    /// Replaces the segments with an empty path segment.
    /// </summary>
    internal void ReplaceSegmentsWithEmptyPath()
    {
        Debug.Assert(this.segments.Count == 0, "segments must be empty to insert an empty path segment");
        this.segments.Add(new UrlSegment(string.Empty));
    }

    /// <summary>
    /// Serializes the <see cref="UrlSegmentGroup"/> into a string, ensuring that its outlet
    /// name and segments are percent-encoded as required.
    /// </summary>
    /// <returns>
    /// A string representing the serialized form of the <see cref="UrlSegmentGroup"/>.
    /// </returns>
    private string Serialize()
    {
        if (this.Children.Count == 0)
        {
            return this.SerializeSegments();
        }

        var childrenAsString = this.children.OrderBy(pair => pair.Key.IsNotPrimary)
            .Select(
                pair => pair.Key.IsPrimary
                    ? string.Concat(pair.Value.ToString())
                    : string.Concat(Uri.EscapeDataString(pair.Key), ':', pair.Value.ToString()))
            .ToList();

        var hasPrimaryChild = this.children.ContainsKey(OutletName.Primary);

        return $"{this.SerializeSegments()}{(hasPrimaryChild ? "/(" : '(')}{string.Join("//", childrenAsString)})";
    }

    /// <summary>
    /// Serializes the <see cref="UrlSegmentGroup"/> as the root segment group.
    /// </summary>
    /// <returns>
    /// A string representing the serialized form of the root <see cref="UrlSegmentGroup"/>.
    /// </returns>
    private string SerializeAsRoot()
    {
        Debug.Assert(
            this.segments.Count == 0,
            "The root `UrlSegmentGroup` should not contain segments. Instead, the segments should be in the `children` so they can be associated with a named outlet.");

        var primary = this.children.TryGetValue(OutletName.Primary, out var primaryChild)
            ? primaryChild.ToString() ?? string.Empty
            : string.Empty;

        var nonPrimary = string.Join(
            "//",
            this.children.Where(pair => pair.Key.IsNotPrimary)
                .Select(pair => string.Concat(Uri.EscapeDataString(pair.Key), ':', pair.Value.ToString())));

        return nonPrimary.Length > 0 ? $"{primary}({nonPrimary})" : primary;
    }

    /// <summary>
    /// Serializes the segments of the <see cref="UrlSegmentGroup"/> into a string.
    /// </summary>
    /// <returns>
    /// A string representing the serialized form of the segments.
    /// </returns>
    private string SerializeSegments() => string.Join('/', this.Segments.Select(s => s.ToString()));
}
