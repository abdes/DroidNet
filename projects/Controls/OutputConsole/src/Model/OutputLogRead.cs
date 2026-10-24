// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Controls.OutputConsole.Model;

/// <summary>
///     The result of <see cref="OutputLogBuffer.ReadSince" />: the entries appended after a sequence, and what
///     the buffer still retains.
/// </summary>
/// <param name="Entries">The entries with a greater sequence than the one asked for, oldest first.</param>
/// <param name="FirstRetainedSequence">
///     The sequence of the oldest retained entry, or <see cref="OutputLogBuffer.LastSequence" /> + 1 when the
///     buffer is empty. Every entry with a smaller sequence has been evicted or cleared.
/// </param>
/// <param name="ClearGeneration">The number of times the buffer has been cleared.</param>
public readonly record struct OutputLogRead(
    IReadOnlyList<OutputLogEntry> Entries,
    long FirstRetainedSequence,
    int ClearGeneration);
