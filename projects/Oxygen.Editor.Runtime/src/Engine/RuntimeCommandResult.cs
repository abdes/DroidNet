// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Runtime.Engine;

/// <summary>A correlated runtime outcome, distinct from asset loading and visible presentation.</summary>
/// <param name="OperationId">The caller's operation identity.</param>
/// <param name="RunId">The requested run.</param>
/// <param name="Status">The boundary outcome.</param>
/// <param name="Message">Failure detail, if any.</param>
/// <param name="Exception">The original failure, if any.</param>
public sealed record RuntimeCommandResult(Guid OperationId, Guid RunId, RuntimeCommandStatus Status, string? Message = null, Exception? Exception = null)
{
    /// <summary>Gets the requested scene activation for a world operation.</summary>
    public RuntimeSceneTarget? SceneTarget { get; init; }

    /// <summary>Gets the requested view generation for an input operation.</summary>
    public RuntimeViewTarget? ViewTarget { get; init; }

    /// <summary>Gets a value indicating whether the command was accepted.</summary>
    public bool Succeeded => this.Status == RuntimeCommandStatus.Accepted;
}
