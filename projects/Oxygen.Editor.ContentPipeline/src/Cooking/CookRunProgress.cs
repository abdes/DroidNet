// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.ContentPipeline.Cooking;

/// <summary>Reports an execution phase, message, asset, or issue to its owning run.</summary>
/// <param name="Message">Optional transcript text.</param>
/// <param name="Severity">The message severity.</param>
/// <param name="State">Optional execution phase change.</param>
/// <param name="Asset">Optional individual asset state.</param>
/// <param name="Diagnostic">Optional structured recovery issue.</param>
public sealed record CookRunProgress(
    string? Message = null,
    DiagnosticSeverity Severity = DiagnosticSeverity.Info,
    CookRunState? State = null,
    CookRunAsset? Asset = null,
    DiagnosticRecord? Diagnostic = null)
{
    /// <summary>Gets the recovery scope after original source has been retained.</summary>
    public CookRunRequest? RecoveryRequest { get; init; }
}
