// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.RegularExpressions;

namespace Oxygen.Editor.World;

/// <summary>
/// LINQ-style helpers for navigating scene node trees and simple path queries.
/// </summary>
public static class SceneNodeExtensions
{
    /// <summary>
    /// Return the node and all its descendants (depth-first).
    /// </summary>
    /// <param name="node">The node to enumerate from.</param>
    /// <returns>An enumerable that yields the node itself followed by its descendants.</returns>
    public static IEnumerable<SceneNode> DescendantsAndSelf(this SceneNode node)
    {
        yield return node;
        foreach (var d in node.Descendants())
        {
            yield return d;
        }
    }

    /// <summary>
    /// Flattened descendant enumeration (alias to instance method).
    /// </summary>
    /// <param name="node">The node to enumerate descendants for.</param>
    /// <returns>A sequence of descendant nodes.</returns>
    public static IEnumerable<SceneNode> Descendants(this SceneNode node)
        => node.Descendants();

    /// <summary>
    /// Return the node and all ancestors (closest parent first).
    /// </summary>
    /// <param name="node">The node to enumerate ancestors for.</param>
    /// <returns>An enumerable that yields the node itself followed by its ancestors (closest parent first).</returns>
    public static IEnumerable<SceneNode> AncestorsAndSelf(this SceneNode node)
    {
        yield return node;
        foreach (var a in node.Ancestors())
        {
            yield return a;
        }
    }

    /// <summary>
    /// Alias to instance Ancestors() for LINQ-style call sites.
    /// </summary>
    /// <param name="node">The node to enumerate ancestors for.</param>
    /// <returns>A sequence of ancestor nodes.</returns>
    public static IEnumerable<SceneNode> Ancestors(this SceneNode node)
        => node.Ancestors();

    /// <summary>
    /// Find a node by a simple path expression.
    /// Supported tokens:
    /// - Exact segment names separated with '/'
    /// - '*' matches any single segment
    /// - '**' matches any sequence of segments (including none)
    /// The path is matched against the node's path relative to scene roots. Leading/trailing slashes are ignored.
    /// </summary>
    /// <param name="scene">The scene to search within.</param>
    /// <param name="path">The path pattern to match.</param>
    /// <returns>The first matching <see cref="SceneNode"/> or <see langword="null"/> when none matched.</returns>
    public static SceneNode? FindByPath(this Scene scene, string path)
    {
        ArgumentNullException.ThrowIfNull(scene);
        ArgumentNullException.ThrowIfNull(path);

        // Normalize: trim slashes and split
        var raw = path.Trim('/');
        if (string.IsNullOrEmpty(raw))
        {
            return null;
        }

        var patternSegments = raw.Split('/', StringSplitOptions.RemoveEmptyEntries);

        // Build regex from pattern segments to match full path (root-based)
        // Escape literal segments; '*' -> [^/]+ ; '**' -> (?:.+)?
        var regexParts = new List<string>();
        foreach (var seg in patternSegments)
        {
            if (string.Equals(seg, "**", StringComparison.Ordinal))
            {
                regexParts.Add("(?:.*)");
            }
            else if (string.Equals(seg, "*", StringComparison.Ordinal))
            {
                regexParts.Add("[^/]+");
            }
            else
            {
                regexParts.Add(Regex.Escape(seg));
            }
        }

        var regexPattern = "^" + string.Join('/', regexParts) + "$";

        // Create a regex with a modest timeout to avoid potential ReDoS attacks on crafted input.
        var regex = new Regex(regexPattern, RegexOptions.IgnoreCase | RegexOptions.CultureInvariant | RegexOptions.Compiled, TimeSpan.FromMilliseconds(250));

        // For each node compute its path (root/.../node) and test against regex
        foreach (var node in scene.AllNodes)
        {
            // compute path by walking ancestors to root
            var segments = new List<string>();
            var cur = node;
            while (cur != null)
            {
                segments.Add(cur.Name ?? string.Empty);
                cur = cur.Parent;
            }

            segments.Reverse();
            var nodePath = string.Join('/', segments);
            if (regex.IsMatch(nodePath))
            {
                return node;
            }
        }

        return null;
    }
}
