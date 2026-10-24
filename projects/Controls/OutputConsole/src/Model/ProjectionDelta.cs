// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Controls.OutputConsole.Model;

/// <summary>
///     The changes <see cref="OutputLogProjection.Append" /> made to its rows, to apply to the view's row
///     collection in this order.
/// </summary>
/// <param name="RemovedFromHead">The number of rows removed from the start.</param>
/// <param name="HeadChanged">Whether the first remaining row lost evicted entries.</param>
/// <param name="TailChanged">Whether the last row that existed before gained folded entries.</param>
/// <param name="Added">The rows added at the end.</param>
internal readonly record struct ProjectionDelta(
    int RemovedFromHead,
    bool HeadChanged,
    bool TailChanged,
    IReadOnlyList<OutputLogRow> Added);
