// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.UI.Xaml.Controls;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.World.Inspection;

/// <summary>A native inspection/validation issue with its physical root context.</summary>
/// <param name="RootName">The owning root.</param>
/// <param name="Diagnostic">The reported issue.</param>
public sealed record InspectionIssueRow(string RootName, DiagnosticRecord Diagnostic)
{
    /// <summary>Gets the WinUI severity for the reported problem.</summary>
    public InfoBarSeverity Severity => this.Diagnostic.Severity switch
    {
        DiagnosticSeverity.Error or DiagnosticSeverity.Fatal => InfoBarSeverity.Error,
        DiagnosticSeverity.Warning => InfoBarSeverity.Warning,
        _ => InfoBarSeverity.Informational,
    };
}
