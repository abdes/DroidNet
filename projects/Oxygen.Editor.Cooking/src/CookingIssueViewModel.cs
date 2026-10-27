// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.ObjectModel;
using System.Globalization;
using CommunityToolkit.Mvvm.ComponentModel;
using Microsoft.UI.Xaml;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.Cooking;

/// <summary>Presents one kind of issue with where it applies and its scoped recovery action.</summary>
/// <param name="diagnostic">The first diagnostic of the issue, including its navigation identity.</param>
/// <param name="scope">The name of the asset or scene it applies to.</param>
public sealed partial class CookingIssueViewModel(DiagnosticRecord diagnostic, string scope) : ObservableObject
{
    private int count = 1;

    /// <summary>Gets the first structured diagnostic of this issue.</summary>
    public DiagnosticRecord Diagnostic { get; } = diagnostic;

    /// <summary>Gets the asset or scene the first diagnostic applies to.</summary>
    public string Scope { get; } = scope;

    /// <summary>Gets the readable problem.</summary>
    public string Message => this.Diagnostic.Message;

    /// <summary>Gets where each summarized diagnostic applies, in the order they were reported.</summary>
    public ObservableCollection<CookingIssuePlace> Places { get; } = [CookingIssuePlace.Of(diagnostic, scope)];

    /// <summary>Gets how many diagnostics this issue summarizes.</summary>
    public int Count => this.count;

    /// <summary>Gets the count badge's text.</summary>
    public string CountText => string.Create(CultureInfo.CurrentCulture, $"×{this.count}");

    /// <summary>Gets the issue as plain text for copying: the message, how many places, the scope and every place.</summary>
    public string CopyText
    {
        get
        {
            var lines = new List<string> { this.Message, string.Create(CultureInfo.CurrentCulture, $"Places: {this.Places.Count}") };
            if (!string.IsNullOrWhiteSpace(this.Scope))
            {
                lines.Add("Scope: " + this.Scope);
            }

            lines.Add(string.Empty);
            lines.AddRange(this.Places.Select(static place => place.Line));
            return string.Join(Environment.NewLine, lines);
        }
    }

    /// <summary>Gets visibility of the count badge.</summary>
    public Visibility CountVisibility => this.count > 1 ? Visibility.Visible : Visibility.Collapsed;

    /// <summary>Gets what the row says under the message: the scope and the kinds of asset it applies to.</summary>
    public string Caption => string.Join(" · ", new[] { this.Scope, string.Join(", ", this.Places.Select(static place => place.Kind).Where(static kind => kind is not null).Distinct(StringComparer.Ordinal)) }.Where(static part => !string.IsNullOrWhiteSpace(part)));

    /// <summary>Gets visibility of the caption.</summary>
    public Visibility CaptionVisibility => string.IsNullOrWhiteSpace(this.Caption) ? Visibility.Collapsed : Visibility.Visible;

    /// <summary>Gets visibility of the error icon.</summary>
    public Visibility ErrorVisibility => this.Rank == 0 ? Visibility.Visible : Visibility.Collapsed;

    /// <summary>Gets visibility of the warning icon.</summary>
    public Visibility WarningVisibility => this.Rank == 1 ? Visibility.Visible : Visibility.Collapsed;

    /// <summary>Gets visibility of the information icon.</summary>
    public Visibility InfoVisibility => this.Rank == 2 ? Visibility.Visible : Visibility.Collapsed;

    /// <summary>Gets the available action's label.</summary>
    public string ActionLabel => this.Diagnostic.SuggestedAction?.Label ?? string.Empty;

    /// <summary>Gets visibility for a supported property navigation action; a summary has no single target.</summary>
    public Visibility ActionVisibility => this.count == 1 && string.Equals(this.Diagnostic.SuggestedAction?.ActionId, "Cook.GoToProperty", StringComparison.Ordinal) ? Visibility.Visible : Visibility.Collapsed;

    /// <summary>Gets the sort rank: errors, then warnings, then the rest.</summary>
    internal int Rank => this.Diagnostic.Severity is DiagnosticSeverity.Error or DiagnosticSeverity.Fatal ? 0 : this.Diagnostic.Severity == DiagnosticSeverity.Warning ? 1 : 2;

    /// <summary>Tells whether a diagnostic says the same thing as this issue.</summary>
    /// <param name="other">The diagnostic to compare.</param>
    /// <returns><see langword="true"/> when the severity, code and message match.</returns>
    internal bool Summarizes(DiagnosticRecord other)
        => other.Severity == this.Diagnostic.Severity
        && string.Equals(other.Code, this.Diagnostic.Code, StringComparison.Ordinal)
        && string.Equals(other.Message, this.Diagnostic.Message, StringComparison.Ordinal);

    /// <summary>Counts one more diagnostic into this issue and keeps where it applies.</summary>
    /// <param name="other">The diagnostic that repeats this issue.</param>
    /// <param name="scope">The name of the asset or scene it applies to.</param>
    internal void Absorb(DiagnosticRecord other, string scope)
    {
        this.count++;
        this.Places.Add(CookingIssuePlace.Of(other, scope));
        this.OnPropertyChanged(string.Empty);
    }
}
