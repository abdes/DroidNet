// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Runtime.InteropServices;

namespace Oxygen.Editor.World.Services;

/// <summary>Captures authoring identity and delivery order before asynchronous sync work.</summary>
/// <param name="DocumentLifetime">The owning open-document lifetime.</param>
/// <param name="Revision">The committed authoring revision.</param>
/// <param name="Sequence">The order of previews and terminal edits within a revision.</param>
[StructLayout(LayoutKind.Auto)]
public readonly record struct SceneSyncRevision(Guid DocumentLifetime, long Revision, long Sequence)
{
    /// <summary>Determines whether a matching-lifetime projection covers this request.</summary>
    /// <param name="other">The accepted snapshot or newer request.</param>
    /// <returns>Whether the request is at or before the supplied boundary.</returns>
    public bool IsAtOrBefore(SceneSyncRevision other)
        => this.DocumentLifetime == other.DocumentLifetime
            && (this.Revision < other.Revision || (this.Revision == other.Revision && this.Sequence <= other.Sequence));
}
