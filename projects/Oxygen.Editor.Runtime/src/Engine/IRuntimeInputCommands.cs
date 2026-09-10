// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Runtime.Engine;

/// <summary>Managed input dispatch with runtime and view-lifetime validation.</summary>
public interface IRuntimeInputCommands
{
    /// <summary>Resolves a live view to its generation-bound input target.</summary>
    /// <param name="viewId">The process-local view identifier.</param>
    /// <returns>The target, or null when the view is unavailable.</returns>
    public RuntimeViewTarget? GetViewTarget(ulong viewId);

    /// <summary>Dispatches input only while its original run and view generation remain current.</summary>
    /// <param name="request">The managed input request.</param>
    /// <param name="cancellationToken">Cancels before dispatch.</param>
    /// <returns>The correlated outcome.</returns>
    public RuntimeCommandResult Execute(RuntimeInputRequest request, CancellationToken cancellationToken = default);
}
