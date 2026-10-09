// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Data;
using Oxygen.Editor.Controls;
using EditorPropertiesExpander = Oxygen.Editor.Controls.PropertiesExpander;
using Expander = Microsoft.UI.Xaml.Controls.Expander;

namespace Oxygen.Editor.World.Inspector.Presentation;

/// <summary>Connects explicitly registered section-owned controls to headless browsing state.</summary>
internal sealed class InspectorSceneFieldRegistry(InspectorSearchModel search, IValueConverter visibility)
{
    /// <summary>Gets explicit field roots keyed by canonical presentation identity.</summary>
    internal Dictionary<string, FrameworkElement> Fields { get; } = new(StringComparer.Ordinal);

    /// <summary>Gets native section controls.</summary>
    internal Dictionary<string, EditorPropertiesExpander> Sections { get; } = new(StringComparer.Ordinal);

    /// <summary>Gets the views owning section namescopes.</summary>
    internal Dictionary<string, UserControl> SectionViews { get; } = new(StringComparer.Ordinal);

    /// <summary>Gets nested field-group disclosures.</summary>
    internal Dictionary<string, Expander> Disclosures { get; } = new(StringComparer.Ordinal);

    /// <summary>Gets the two independently restored source disclosures.</summary>
    internal Dictionary<string, Expander> Sources { get; } = new(StringComparer.Ordinal);

    /// <summary>Registers a native section and its owning view.</summary>
    /// <param name="key">The stable section identity.</param>
    /// <param name="view">The owning namescope.</param>
    /// <param name="section">The original native section control.</param>
    internal void Section(string key, UserControl view, EditorPropertiesExpander section)
    {
        this.Sections.Add(key, section);
        this.SectionViews.Add(key, view);
    }

    /// <summary>Binds an explicit field root and optional applicability note to headless presentation state.</summary>
    /// <param name="key">The stable presentation identity.</param>
    /// <param name="fieldRoot">The view-owned field composition.</param>
    /// <param name="note">The existing declared applicability note.</param>
    internal void Field(string key, FrameworkElement fieldRoot, TextBlock? note = null)
    {
        this.Fields.Add(key, fieldRoot);
        var field = search.Fields[key];
        fieldRoot.SetBinding(UIElement.VisibilityProperty, new Binding
        {
            Source = field,
            Path = new PropertyPath(nameof(InspectorFieldPresentation.IsVisible)),
            Mode = BindingMode.OneWay,
            Converter = visibility,
        });
        if (fieldRoot is InspectorNumberField number)
        {
            number.SetBinding(InspectorNumberField.ApplicabilityTextProperty, new Binding
            {
                Source = field,
                Path = new PropertyPath(nameof(InspectorFieldPresentation.ApplicabilityText)),
                Mode = BindingMode.OneWay,
            });
        }

        if (note is not null)
        {
            note.SetBinding(TextBlock.TextProperty, new Binding
            {
                Source = field,
                Path = new PropertyPath(nameof(InspectorFieldPresentation.ApplicabilityText)),
                Mode = BindingMode.OneWay,
            });
            note.SetBinding(UIElement.VisibilityProperty, new Binding
            {
                Source = field,
                Path = new PropertyPath(nameof(InspectorFieldPresentation.HasApplicabilityText)),
                Mode = BindingMode.OneWay,
                Converter = visibility,
            });
        }
    }

    /// <summary>Applies browsing state without constructing, reparenting or replacing editors.</summary>
    /// <param name="query">The property query.</param>
    /// <param name="scope">The current property scope.</param>
    /// <param name="modes">The authored modes that decide which stored fields apply.</param>
    internal void Apply(string query, InspectorPropertyScope scope, InspectorApplicabilityContext modes)
    {
        foreach (var (key, section) in this.Sections)
        {
            search.RecordExpansion(key, section.IsExpanded);
        }

        foreach (var (key, disclosure) in this.Disclosures.Concat(this.Sources))
        {
            search.RecordExpansion(key, disclosure.IsExpanded);
        }

        search.Update(query, scope, modes);
        foreach (var (key, section) in this.Sections)
        {
            section.Visibility = search.IsGroupVisible(key) ? Visibility.Visible : Visibility.Collapsed;
            section.IsExpanded = search.IsExpanded(key);
        }

        foreach (var view in this.SectionViews.Values.Distinct())
        {
            view.Visibility = this.Sections.Any(pair => ReferenceEquals(this.SectionViews[pair.Key], view)
                && pair.Value.Visibility == Visibility.Visible) ? Visibility.Visible : Visibility.Collapsed;
        }

        foreach (var (key, disclosure) in this.Disclosures)
        {
            disclosure.Visibility = search.IsGroupVisible(key) ? Visibility.Visible : Visibility.Collapsed;
            disclosure.IsExpanded = search.IsExpanded(key);
        }

        var reveal = search.IsSearching && search.IsGroupVisible("AtmosphereLights");
        foreach (var (key, source) in this.Sources)
        {
            source.IsExpanded = reveal || search.IsExpanded(key);
        }
    }
}
