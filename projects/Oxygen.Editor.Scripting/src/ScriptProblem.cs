// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Managed.Assets.Diagnostics;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.Scripting;

/// <summary>
/// One problem the engine's script analyzer reports, with its code (such as <c>S0503</c>), its position in the script
/// and the diagnostic as the engine renders it.
/// </summary>
public record ScriptProblem : AssetProblem
{
    /// <summary>Initializes a new instance of the <see cref="ScriptProblem"/> class.</summary>
    /// <param name="severity">How serious the problem is.</param>
    /// <param name="code">The engine code.</param>
    /// <param name="message">One line, lower case.</param>
    /// <param name="rendered">The full diagnostic with source excerpts, in the engine's layout.</param>
    public ScriptProblem(DiagnosticSeverity severity, string code, string message, string rendered)
        : base(severity, code, message)
    {
        ArgumentNullException.ThrowIfNull(rendered);
        this.Rendered = rendered;
    }

    /// <summary>Gets the full diagnostic with source excerpts, in the engine's layout.</summary>
    public string Rendered { get; init; }
}
