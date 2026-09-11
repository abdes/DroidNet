// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.World.Cooking;

/// <summary>Presents a short issue and its scoped recovery action.</summary>
/// <param name="diagnostic">The immutable diagnostic, including its navigation identity.</param>
public sealed class CookingIssueViewModel(DiagnosticRecord diagnostic)
{
    /// <summary>Gets the originating structured diagnostic.</summary>
    public DiagnosticRecord Diagnostic { get; } = diagnostic;

    /// <summary>Gets the readable problem.</summary>
    public string Message => this.Diagnostic.Message;

    /// <summary>Gets the native InfoBar severity for this issue.</summary>
    public InfoBarSeverity Severity => this.Diagnostic.Severity is DiagnosticSeverity.Error or DiagnosticSeverity.Fatal
        ? InfoBarSeverity.Error : this.Diagnostic.Severity == DiagnosticSeverity.Warning ? InfoBarSeverity.Warning : InfoBarSeverity.Informational;

    /// <summary>Gets the available action's label.</summary>
    public string ActionLabel => this.Diagnostic.SuggestedAction?.Label ?? string.Empty;

    /// <summary>Gets visibility for a supported property navigation action.</summary>
    public Visibility ActionVisibility => string.Equals(this.Diagnostic.SuggestedAction?.ActionId, "Cook.GoToProperty", StringComparison.Ordinal) ? Visibility.Visible : Visibility.Collapsed;
}
