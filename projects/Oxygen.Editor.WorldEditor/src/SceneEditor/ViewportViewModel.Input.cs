// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Runtime.Engine;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.LevelEditor;

/// <summary>Routes input through the managed capability and the existing operation-result surface.</summary>
public partial class ViewportViewModel
{
    private (RuntimeViewTarget target, RuntimeCommandStatus status, string? message)? lastInputFailure;

    /// <summary>Forwards input for the captured view generation and reports rejection once per failure.</summary>
    /// <param name="target">The original view generation.</param>
    /// <param name="input">The managed input intent in physical pixels.</param>
    public void ForwardInput(RuntimeViewTarget target, RuntimeInputEvent input)
    {
        var result = this.EngineService.InputCommands.Execute(new RuntimeInputRequest(Guid.NewGuid(), target, input));
        if (result.Succeeded)
        {
            this.lastInputFailure = null;
            return;
        }

        var failure = (target, result.Status, result.Message);
        if (result.Status == RuntimeCommandStatus.Cancelled || this.lastInputFailure == failure)
        {
            return;
        }

        this.lastInputFailure = failure;
        this.PublishRuntimeWarning(
            "Runtime.Input.Dispatch",
            FailureDomain.RuntimeView,
            DiagnosticCodes.ViewPrefix + "INPUT_" + result.Status.ToString().ToUpperInvariant(),
            "Viewport input could not be applied",
            result.Message ?? "The runtime did not accept input for this viewport.",
            result.Exception);
    }
}
