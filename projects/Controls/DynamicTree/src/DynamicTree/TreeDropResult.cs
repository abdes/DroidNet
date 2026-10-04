// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Controls;

/// <summary>Result of a committed or rejected tree drop.</summary>
/// <param name="Succeeded">Whether the request was committed.</param>
/// <param name="Items">The resulting moved or created roots.</param>
public sealed record TreeDropResult(bool Succeeded, IReadOnlyList<ITreeItem> Items)
{
    /// <summary>Gets a rejected drop result.</summary>
    public static TreeDropResult Rejected { get; } = new(Succeeded: false, []);

    /// <summary>Creates a successful drop result.</summary>
    /// <param name="items">The moved or created roots.</param>
    /// <returns>The committed result.</returns>
    public static TreeDropResult Committed(IReadOnlyList<ITreeItem> items) => new(Succeeded: true, Array.AsReadOnly(items.ToArray()));
}
