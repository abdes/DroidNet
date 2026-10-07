// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.World.Inspector.Editing;

/// <summary>Refreshes one typed inspector binding regardless of its value type.</summary>
internal interface IInspectorBindingRefresh : IDisposable
{
    /// <summary>Refreshes canonical state while ignoring two-way control echoes.</summary>
    /// <param name="nodes">Captured contributing node identities.</param>
    /// <param name="targets">The current typed target lookup.</param>
    public void Refresh(IReadOnlyList<Guid> nodes, Func<Guid, object?> targets);
}
