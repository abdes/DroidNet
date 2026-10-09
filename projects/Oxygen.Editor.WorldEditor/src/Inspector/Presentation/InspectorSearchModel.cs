// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.Mvvm.ComponentModel;

namespace Oxygen.Editor.World.Inspector.Presentation;

/// <summary>Scene-inspector search, applicability and temporary disclosure state.</summary>
public sealed partial class InspectorSearchModel : ObservableObject
{
    private readonly Dictionary<string, bool> expansion = new(StringComparer.Ordinal);
    private readonly Dictionary<string, bool> savedExpansion = new(StringComparer.Ordinal);
    private string[] terms = [];

    /// <summary>Initializes a new instance of the <see cref="InspectorSearchModel"/> class.</summary>
    /// <param name="fields">Explicit metadata for the existing field compositions.</param>
    public InspectorSearchModel(IReadOnlyList<InspectorFieldPresentation> fields)
    {
        ArgumentNullException.ThrowIfNull(fields);
        this.Fields = fields.ToDictionary(static field => field.Key, StringComparer.Ordinal);
    }

    /// <summary>Gets field presentation by stable presentation identity.</summary>
    public IReadOnlyDictionary<string, InspectorFieldPresentation> Fields { get; }

    /// <summary>Gets a value indicating whether a nonempty tokenized query is active.</summary>
    public bool IsSearching => this.terms.Length > 0;

    /// <summary>Gets a value indicating whether a search has no matches in the selected scope.</summary>
    public bool HasNoMatches => this.IsSearching && !this.Fields.Values.Any(static entry => entry.IsVisible);

    /// <summary>Updates the existing AND-token search and authored-mode applicability.</summary>
    /// <param name="query">The query text.</param>
    /// <param name="scope">The browsing scope.</param>
    /// <param name="modes">The authored modes that decide which stored fields apply.</param>
    public void Update(string query, InspectorPropertyScope scope, InspectorApplicabilityContext modes)
    {
        var wasSearching = this.IsSearching;
        this.terms = query.Trim().Split([' ', '\t', '/', '_', '-'], StringSplitOptions.RemoveEmptyEntries)
            .Select(Normalize).ToArray();
        if (this.IsSearching && !wasSearching)
        {
            this.savedExpansion.Clear();
            foreach (var (key, expanded) in this.expansion)
            {
                this.savedExpansion.Add(key, expanded);
            }
        }
        else if (!this.IsSearching && wasSearching)
        {
            foreach (var (key, expanded) in this.savedExpansion)
            {
                this.expansion[key] = expanded;
            }

            this.savedExpansion.Clear();
        }

        foreach (var field in this.Fields.Values)
        {
            var applicable = modes.Applies(field.Applicability);
            var matches = this.terms.All(term => field.SearchText.Contains(term, StringComparison.Ordinal));
            field.IsVisible = (scope == InspectorPropertyScope.All || field.Scope == scope)
                && (this.IsSearching ? matches : applicable);
            field.ApplicabilityText = this.IsSearching && matches && !applicable
                ? modes.InapplicableNote(field.Applicability)
                : string.Empty;
        }

        this.OnPropertyChanged(nameof(this.IsSearching));
        this.OnPropertyChanged(nameof(this.HasNoMatches));
    }

    /// <summary>Records user expansion only while no search override is active.</summary>
    /// <param name="key">The section/disclosure identity.</param>
    /// <param name="expanded">The existing expansion state.</param>
    public void RecordExpansion(string key, bool expanded)
    {
        if (!this.IsSearching)
        {
            this.expansion[key] = expanded;
        }
    }

    /// <summary>Gets expansion with a temporary search override for matching fields.</summary>
    /// <param name="key">The section/disclosure identity.</param>
    /// <returns>The preserved or temporary expansion state.</returns>
    public bool IsExpanded(string key)
        => (this.IsSearching && this.IsGroupVisible(key)) || this.expansion.GetValueOrDefault(key);

    /// <summary>Gets whether a section or disclosure contains visible fields.</summary>
    /// <param name="key">The section/disclosure identity.</param>
    /// <returns>Whether any field in the group is visible.</returns>
    public bool IsGroupVisible(string key)
        => this.Fields.Values.Any(entry => entry.IsVisible
            && (string.Equals(entry.Section, key, StringComparison.Ordinal) || string.Equals(entry.Group, key, StringComparison.Ordinal)));

    /// <summary>Gets whether a browsing heading has visible fields.</summary>
    /// <param name="scope">The heading's scope.</param>
    /// <returns>Whether any field in the scope is visible.</returns>
    public bool IsScopeVisible(InspectorPropertyScope scope)
        => this.Fields.Values.Any(field => field.IsVisible && field.Scope == scope);

    /// <summary>Normalizes the existing case/punctuation-insensitive search contract.</summary>
    /// <param name="text">One query token or metadata string.</param>
    /// <returns>Lower-case letters and digits only.</returns>
    internal static string Normalize(string text)
        => new(text.Where(char.IsLetterOrDigit).Select(char.ToLowerInvariant).ToArray());
}
