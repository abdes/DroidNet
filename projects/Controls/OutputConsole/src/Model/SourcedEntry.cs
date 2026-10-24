// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Controls.OutputConsole.Model;

/// <summary>
///     An entry with its position in the console's source: its <see cref="OutputLogEntry.Sequence" /> for a
///     buffer, or its index plus one for a collection.
/// </summary>
/// <param name="Ordinal">The entry's position; strictly increasing in source order.</param>
/// <param name="Entry">The entry.</param>
internal readonly record struct SourcedEntry(long Ordinal, OutputLogEntry Entry);
