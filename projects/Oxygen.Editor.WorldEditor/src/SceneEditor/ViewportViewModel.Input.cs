// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Runtime.Engine;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.LevelEditor;

/// <summary>Routes input through the managed capability and the existing operation-result surface.</summary>
public partial class ViewportViewModel
{
    private readonly HashSet<RuntimeMouseButton> heldButtons = [];
    private readonly HashSet<RuntimeKey> heldKeys = [];
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
            this.TrackNavigationInput(input);
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

    /// <summary>Feeds navigation gestures to the pilot: buttons, keys, wheel and drags, not hover motion.</summary>
    private void TrackNavigationInput(RuntimeInputEvent input)
    {
        switch (input)
        {
            case RuntimeButtonEvent button:
                _ = button.Pressed ? this.heldButtons.Add(button.Button) : this.heldButtons.Remove(button.Button);
                break;
            case RuntimeKeyEvent key:
                _ = key.Pressed ? this.heldKeys.Add(key.Key) : this.heldKeys.Remove(key.Key);
                break;
            case RuntimeMouseMotionEvent when this.heldButtons.Count == 0:
                return;
            case RuntimeMouseMotionEvent or RuntimeMouseWheelEvent:
                break;
            case RuntimeFocusLostEvent:
                this.heldButtons.Clear();
                this.heldKeys.Clear();
                break;
            default:
                return;
        }

        this.NotifyNavigationInput(inputHeld: this.heldButtons.Count > 0 || this.heldKeys.Count > 0);
    }
}
