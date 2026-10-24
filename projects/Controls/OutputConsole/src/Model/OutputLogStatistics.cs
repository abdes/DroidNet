// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Serilog.Events;

namespace DroidNet.Controls.OutputConsole.Model;

/// <summary>
///     A snapshot of what an <see cref="OutputLogBuffer" /> retains.
/// </summary>
/// <param name="Capacity">The maximum number of retained entries.</param>
/// <param name="Count">The number of retained entries.</param>
/// <param name="EvictedCount">The number of entries overwritten by newer ones since the buffer was created or last cleared.</param>
/// <param name="CountsByLevel">Retained entries per level, indexed by <see cref="LogEventLevel" />.</param>
/// <param name="Subsystems">
///     Retained entries per subsystem, sorted by name; entries without a subsystem are counted last, under a
///     <see langword="null" /> key.
/// </param>
public sealed record OutputLogStatistics(
    int Capacity,
    int Count,
    long EvictedCount,
    IReadOnlyList<int> CountsByLevel,
    IReadOnlyList<KeyValuePair<string?, int>> Subsystems)
{
    /// <summary>
    ///     Gets the number of retained entries at the given level.
    /// </summary>
    /// <param name="level">The level.</param>
    /// <returns>The count.</returns>
    public int CountOf(LogEventLevel level) => this.CountsByLevel[(int)level];
}
