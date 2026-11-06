// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Models;

/// <summary>A model import record that could not be read, or whose source is in a mount this machine has not bound.</summary>
/// <param name="RecordPath">The record's full path.</param>
/// <param name="Reason">Why it could not be read.</param>
public sealed record ModelImportRecordProblem(string RecordPath, string Reason)
{
    /// <summary>Gets the record's source as it spells it, a mount path, when its mount is not bound on this machine; the
    /// record itself is readable. Null when the record could not be read.</summary>
    public string? UnboundSource { get; init; }

    /// <summary>Gets the record's source as it spells it when the reference grammar refuses it; null otherwise.</summary>
    public string? RefusedSource { get; init; }

    /// <summary>Gets the code of the refusal of <see cref="RefusedSource"/>.</summary>
    public string? RefusalCode { get; init; }
}
