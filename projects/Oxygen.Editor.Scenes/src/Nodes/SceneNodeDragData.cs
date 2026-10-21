// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Globalization;
using Oxygen.Managed.Core;
using Windows.ApplicationModel.DataTransfer;

namespace Oxygen.Editor.Scenes.Nodes;

/// <summary>
/// The scene-node drag payload: the nodes a drag carries, written by the part that starts the drag and read by any part
/// that accepts them, such as node fields in the inspector.
/// </summary>
public static class SceneNodeDragData
{
    /// <summary>The data format: the dragged node identities, separated by semicolons.</summary>
    public const string Format = "application/vnd.oxygen.scene-nodes";

    /// <summary>Adds the dragged nodes to a drag's data.</summary>
    /// <param name="data">The drag's data package.</param>
    /// <param name="nodes">The dragged node identities.</param>
    public static void Write(DataPackage data, IEnumerable<NodeId> nodes)
    {
        ArgumentNullException.ThrowIfNull(data);
        ArgumentNullException.ThrowIfNull(nodes);
        var text = string.Join(';', nodes.Select(static node => node.ToString()));
        if (text.Length > 0)
        {
            data.SetData(Format, text);
        }
    }

    /// <summary>Checks whether a drag carries scene nodes.</summary>
    /// <param name="data">The drag's data.</param>
    /// <returns>Whether the drag came from the Scene Explorer with nodes.</returns>
    public static bool Contains(DataPackageView data) => data?.Contains(Format) == true;

    /// <summary>Reads the dragged nodes.</summary>
    /// <param name="data">The drag's data.</param>
    /// <returns>The node identities; empty when the drag carries none.</returns>
    public static async Task<IReadOnlyList<NodeId>> ReadAsync(DataPackageView data)
    {
        if (!Contains(data) || await data.GetDataAsync(Format) is not string text)
        {
            return [];
        }

        return [.. text.Split(';', StringSplitOptions.RemoveEmptyEntries)
            .Select(static part => Guid.TryParse(part, CultureInfo.InvariantCulture, out var node) ? NodeId.FromValue(node) : NodeId.Empty)
            .Where(static node => !node.IsEmpty)];
    }
}
