// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing;

using System.Collections.ObjectModel;
using System.Diagnostics;
using System.Linq;

/// <summary>
/// Represents a parsed sequence of URL segments, forming a path.
/// </summary>
public class UrlSegmentGroup
{
    /// <summary>
    /// A constant representing the primary outlet identifier. Never hard code
    /// the primary outlet id.
    /// </summary>
    public const string PrimaryOutlet = "";

    private readonly Dictionary<string, UrlSegmentGroup> children = [];

    private readonly List<UrlSegment> segments = [];

    /// <summary>
    /// Initializes a new instance of the <see cref="UrlSegmentGroup" /> class.
    /// </summary>
    /// <param name="segments">The URL segments.</param>
    /// <param name="children">The children of this <see cref="UrlSegmentGroup" />.</param>
    public UrlSegmentGroup(IEnumerable<UrlSegment> segments, IDictionary<string, UrlSegmentGroup>? children = null)
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
    /// <value>
    /// A read-only dictionary of this <see cref="UrlSegmentGroup" />'s
    /// children.
    /// </value>
    public IReadOnlyDictionary<string, UrlSegmentGroup> Children => this.children.AsReadOnly();

    /// <summary>
    /// Gets the children of this <see cref="UrlSegmentGroup" /> with the child
    /// corresponding to the primary outlet first.
    /// </summary>
    /// <value>
    /// A read-only collection of this <see cref="UrlSegmentGroup" />'s
    /// children, where the child corresponding to the primary outlet comes
    /// first.
    /// </value>
    public ReadOnlyCollection<KeyValuePair<string, UrlSegmentGroup>> SortedChildren
    {
        get
        {
            UrlSegmentGroup? primary = null;
            if (this.children.TryGetValue(PrimaryOutlet, out var value))
            {
                primary = value;
            }

            var res = this.children.Where(c => c.Key != PrimaryOutlet).ToList();
            if (primary != null)
            {
                res.Insert(0, new KeyValuePair<string, UrlSegmentGroup>(PrimaryOutlet, primary));
            }

            return res.AsReadOnly();
        }
    }

    /// <summary>
    /// Gets the parent <see cref="UrlSegmentGroup" /> of this
    /// <see cref="UrlSegmentGroup" />.
    /// </summary>
    /// <remarks>
    /// The parent is automatically set when this <seealso cref="UrlSegmentGroup" />
    /// is added as a child to another <see cref="UrlSegmentGroup" />.
    /// </remarks>
    /// <value>The parent <see cref="UrlSegmentGroup" />.</value>
    public UrlSegmentGroup? Parent { get; private set; }

    /// <summary>
    /// Gets the segments of this <see cref="UrlSegmentGroup" />.
    /// </summary>
    /// <value>
    /// A read-only collection of this <see cref="UrlSegmentGroup" />'s
    /// segments.
    /// </value>
    public ReadOnlyCollection<UrlSegment> Segments => this.segments.AsReadOnly();

    /// <summary>
    /// Gets a value indicating whether this segment group is relative (i.e.
    /// starts with a double dot path).
    /// </summary>
    /// <value>
    /// <c>true</c> if the first segment in the group in a double dot.
    /// <c>false</c> otherwise.
    /// </value>
    public bool IsRelative => this.segments.Count > 0 && this.segments[0].Path is "..";

    /// <summary>
    /// Adds a child <see cref="UrlSegmentGroup" /> to the current
    /// <see cref="UrlSegmentGroup" />.
    /// </summary>
    /// <param name="outlet">The outlet name.</param>
    /// <param name="child">The child <see cref="UrlSegmentGroup" />.</param>
    public void AddChild(string outlet, UrlSegmentGroup child)
    {
        this.children.Add(outlet, child);
        child.Parent = this;
    }

    /// <summary>
    /// Serializes the <see cref="UrlSegmentGroup" /> into a string, making
    /// sure that its outlet name and segments are percent-encoded as required.
    /// required.
    /// </summary>
    /// <returns>
    /// A string representing the serialized form of the <see cref="UrlSegmentGroup" />
    /// .
    /// </returns>
    public override string ToString() => this.segments.Count == 0 ? this.SerializeAsRoot() : this.Serialize();

    private string Serialize()
    {
        Debug.Assert(this.segments.Count > 0, "Use SerializeAsRoot() if a `UrlSegmentGroup` contains segments");

        if (this.Children.Count == 0)
        {
            return this.SerializeSegments();
        }

        var childrenAsString = this.children.OrderBy(pair => pair.Key != PrimaryOutlet)
            .Select(
                pair => pair.Key == PrimaryOutlet
                    ? string.Concat(pair.Value.ToString())
                    : string.Concat(Uri.EscapeDataString(pair.Key), ':', pair.Value.ToString()))
            .ToList();
        if (this.children.Count == 1 && this.children.ContainsKey(PrimaryOutlet))
        {
            return string.Concat(this.SerializeSegments(), '/', childrenAsString[0]);
        }

        return string.Concat(this.SerializeSegments(), '(', string.Join("//", childrenAsString), ')');
    }

    private string SerializeAsRoot()
    {
        Debug.Assert(
            this.segments.Count == 0,
            "The root `UrlSegmentGroup` should not contain segments. Instead, the segments should be in the `children` so they can be associated with a named outlet.");

        var primary = this.children.TryGetValue(PrimaryOutlet, out var primaryChild)
            ? primaryChild.ToString()
            : string.Empty;

        var nonPrimary = string.Join(
            "//",
            this.children.Where(pair => pair.Key != PrimaryOutlet)
                .Select(pair => string.Concat(Uri.EscapeDataString(pair.Key), ':', pair.Value.ToString())));

        return nonPrimary.Length > 0 ? string.Concat(primary, '(', nonPrimary, ')') : primary;
    }

    private string SerializeSegments() => string.Join('/', this.Segments.Select(s => s.ToString()));
}
