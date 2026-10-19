// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ContentPipeline.Incremental;

/// <summary>What reading a loose cooked root's index found: an index of the current format, or content to recook.</summary>
internal abstract record CookedIndexRead
{
    private CookedIndexRead()
    {
    }

    /// <summary>Gets the index of a root its owner already accepted as readable.</summary>
    /// <param name="root">The root, for the error.</param>
    /// <returns>The index opening.</returns>
    /// <exception cref="InvalidDataException">The root needs recooking.</exception>
    public CookedIndexSnapshot RequireCurrent(string root)
        => this is Current current ? current.Snapshot
            : throw new InvalidDataException($"Cooked content at '{root}' uses another index format; recook it.");

    /// <summary>The index is in the current format.</summary>
    /// <param name="Snapshot">The index opening.</param>
    internal sealed record Current(CookedIndexSnapshot Snapshot) : CookedIndexRead;

    /// <summary>The root was cooked in another index format and needs recooking; nothing in it is readable.</summary>
    /// <param name="FormatVersion">The format version the index declares.</param>
    internal sealed record Stale(ushort FormatVersion) : CookedIndexRead;
}
