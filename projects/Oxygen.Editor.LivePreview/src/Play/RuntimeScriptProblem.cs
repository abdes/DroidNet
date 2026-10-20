// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Scripting;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.LivePreview.Play;

/// <summary>
/// A problem the running scene reported while it played: a script's runtime error, or a part of the scene the player
/// could not apply. It names the node, and the script slot when there is one.
/// </summary>
/// <remarks>The player names nodes, not identities; <see cref="IRuntimeProblemSource.NodeOf"/> finds the node.</remarks>
public sealed record RuntimeScriptProblem : ScriptProblem
{
    /// <summary>Initializes a new instance of the <see cref="RuntimeScriptProblem"/> class.</summary>
    /// <param name="severity">How serious the problem is.</param>
    /// <param name="code">The engine code.</param>
    /// <param name="message">One line, lower case.</param>
    /// <param name="rendered">The full diagnostic with source excerpts, in the engine's layout.</param>
    public RuntimeScriptProblem(DiagnosticSeverity severity, string code, string message, string rendered)
        : base(severity, code, message, rendered)
    {
    }

    /// <summary>Gets the name of the node the problem is on, when the player names one.</summary>
    public string? SlotNode { get; init; }

    /// <summary>Gets the zero-based position of the script slot on its node, when the problem is a script's.</summary>
    public int? SlotIndex { get; init; }

    /// <summary>Gets the script hook that was running, such as <c>on_update</c>, when the player names it.</summary>
    public string? SlotHook { get; init; }
}
