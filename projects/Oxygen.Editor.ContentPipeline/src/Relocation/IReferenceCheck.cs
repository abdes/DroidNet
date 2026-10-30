// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ContentPipeline.Relocation;

/// <summary>
/// The reference check of the session's project: every reference of its authored files that does not resolve in its
/// source mounts. The session runs it when the project opens and after a mount change; a user runs it on demand.
/// </summary>
public interface IReferenceCheck
{
    /// <summary>Gets the references the last check found unresolved; empty before the first check.</summary>
    public IReadOnlyList<UnresolvedReference> Unresolved { get; }

    /// <summary>
    /// Checks every reference of the project's authored files, lists the unresolved ones in the Problems pane, and
    /// writes them to <c>.build/references.json</c>.
    /// </summary>
    /// <param name="cancellationToken">Cancels the check.</param>
    /// <returns>The unresolved references.</returns>
    public Task<IReadOnlyList<UnresolvedReference>> RunAsync(CancellationToken cancellationToken);
}
