// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Core.Diagnostics;

namespace Oxygen.Editor.ProjectBrowser.Activation;

/// <summary>
/// Coordinates project open/create requests into workspace activation.
/// </summary>
public interface IProjectActivationCoordinator
{
    /// <summary>
    /// Activates a project request and publishes one top-level operation result.
    /// </summary>
    /// <param name="request">The activation request.</param>
    /// <param name="cancellationToken">A cancellation token.</param>
    /// <returns>The operation result for the activation attempt.</returns>
    public Task<OperationResult> ActivateAsync(
        ProjectActivationRequest request,
        CancellationToken cancellationToken = default);
}
