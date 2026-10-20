// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Managed.Assets.Diagnostics;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.Scripting;

/// <summary>
/// A problem of a scene's script slot, or of one of its values, with one of the slot codes of
/// <see cref="ScriptDiagnosticCodes"/>. The message names the parameter but not the node, which the reporter names.
/// </summary>
public sealed record ScriptSlotProblem : AssetProblem
{
    /// <summary>Initializes a new instance of the <see cref="ScriptSlotProblem"/> class.</summary>
    /// <param name="severity">How serious the problem is.</param>
    /// <param name="code">The slot code.</param>
    /// <param name="message">What is wrong, for the user.</param>
    /// <param name="field">The slot, or the parameter field, that has the problem.</param>
    public ScriptSlotProblem(DiagnosticSeverity severity, string code, string message, ScriptFieldAddress field)
        : base(severity, code, message)
    {
        this.Field = field;
    }

    /// <summary>Gets the slot, or the parameter field, that has the problem.</summary>
    public ScriptFieldAddress Field { get; }
}
