// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.Mvvm.ComponentModel;
using Oxygen.Editor.Schemas;

namespace Oxygen.Editor.World.Inspector.Presentation;

/// <summary>Search metadata and observable presentation for one existing field.</summary>
public sealed partial class InspectorFieldPresentation : ObservableObject
{
    private bool isVisible = true;
    private string applicabilityText = string.Empty;

    /// <summary>Initializes a new instance of the <see cref="InspectorFieldPresentation"/> class.</summary>
    /// <param name="key">The presentation identity, distinct from the property identity.</param>
    /// <param name="property">The canonical authored property.</param>
    /// <param name="scope">The browsing scope.</param>
    /// <param name="section">The section identity.</param>
    /// <param name="group">The optional disclosure identity.</param>
    /// <param name="label">The existing displayed label.</param>
    /// <param name="description">The existing section description.</param>
    /// <param name="aliases">Original identifiers and intentional keywords.</param>
    /// <param name="applicability">The existing mode-dependent presentation policy.</param>
    public InspectorFieldPresentation(
        string key,
        PropertyId property,
        InspectorPropertyScope scope,
        string section,
        string? group,
        string label,
        string description,
        string aliases,
        InspectorFieldApplicability applicability = InspectorFieldApplicability.Always)
    {
        this.Key = key;
        this.Property = property;
        this.Scope = scope;
        this.Section = section;
        this.Group = group;
        this.Applicability = applicability;
        var scopeName = scope == InspectorPropertyScope.PostProcessing ? "Post-processing" : "Environment";
        this.SearchText = InspectorSearchModel.Normalize($"{scopeName} {label} {description} {aliases} {property.Pointer}");
    }

    /// <summary>Gets the presentation identity.</summary>
    public string Key { get; }

    /// <summary>Gets the canonical authored identity.</summary>
    public PropertyId Property { get; }

    /// <summary>Gets the browsing scope.</summary>
    public InspectorPropertyScope Scope { get; }

    /// <summary>Gets the section identity.</summary>
    public string Section { get; }

    /// <summary>Gets the optional disclosure identity.</summary>
    public string? Group { get; }

    /// <summary>Gets the authored-mode presentation rule.</summary>
    public InspectorFieldApplicability Applicability { get; }

    /// <summary>Gets a value indicating whether the field is visible in the current search/scope.</summary>
    public bool IsVisible
    {
        get => this.isVisible;
        internal set => this.SetProperty(ref this.isVisible, value);
    }

    /// <summary>Gets the applicability note, separate from validation errors.</summary>
    public string ApplicabilityText
    {
        get => this.applicabilityText;
        internal set
        {
            if (this.SetProperty(ref this.applicabilityText, value))
            {
                this.OnPropertyChanged(nameof(this.HasApplicabilityText));
            }
        }
    }

    /// <summary>Gets a value indicating whether a stored-value applicability note is shown.</summary>
    public bool HasApplicabilityText => this.ApplicabilityText.Length > 0;

    /// <summary>Gets normalized search metadata independent of realized controls.</summary>
    internal string SearchText { get; }
}
