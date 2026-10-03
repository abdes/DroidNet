// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Controls;
using DroidNet.Mvvm.Generators;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Oxygen.Editor.World.Inspector.Geometry;
using Oxygen.Editor.World.Serialization;
using NumberBox = DroidNet.Controls.NumberBox;
using EditorPropertyCard = Oxygen.Editor.Controls.PropertyCard;
using EditorPropertiesExpander = Oxygen.Editor.Controls.PropertiesExpander;
using Expander = Microsoft.UI.Xaml.Controls.Expander;

namespace Oxygen.Editor.World.Inspector;

/// <summary>
/// Scene environment inspector view.
/// </summary>
[ViewModel(typeof(EnvironmentViewModel))]
public sealed partial class EnvironmentView
{
    private readonly Dictionary<EditorPropertiesExpander, bool> expandedBeforeSearch = [];
    private readonly Dictionary<EditorPropertyCard, TextBlock> applicabilityNotes = [];
    private readonly Dictionary<EditorPropertyCard, string> propertySearchText = [];
    private readonly Dictionary<EditorPropertiesExpander, EditorPropertyCard[]> sectionCards = [];
    private readonly Dictionary<Expander, EditorPropertyCard[]> disclosureCards = [];
    private readonly Dictionary<Expander, bool> disclosuresBeforeSearch = [];
    private EnvironmentViewModel? observedModel;
    private bool hasActiveSearch;
    private string propertyScope = "All";

    /// <summary>
    /// Initializes a new instance of the <see cref="EnvironmentView"/> class.
    /// </summary>
    public EnvironmentView()
    {
        this.InitializeComponent();
        InspectorRgbPresentation.Configure(this.GroundAlbedoChannels);
        InspectorRgbPresentation.Configure(this.SkyLuminanceChannels);
        InspectorRgbPresentation.Configure(this.BackgroundChannels);
        this.OrganizeSceneSections();
        _ = this.ScenePropertySearchBox.RegisterPropertyChangedCallback(TextBox.TextProperty, (_, _) => this.ApplyScenePropertyFilter());
        this.Loaded += this.OnLoaded;
        this.Unloaded += this.OnUnloaded;
        this.AerialStartInput.Loaded += (_, _) => this.TryFocusAerialStart();
    }

    private void OrganizeSceneSections()
    {
        var sections = new[] { this.AtmosphereLightsSection, this.SkyAtmosphereSection, this.BackgroundSection, this.ExposureSection, this.ToneMappingSection, this.ColorGradingSection, this.BloomSection };
        foreach (var section in sections)
        {
            this.sectionCards[section] = section.Items.OfType<EditorPropertyCard>().ToArray();
            this.SceneSections.Children.Remove(section);
        }

        this.SceneSections.Children.Remove(this.PostProcessingHeading);
        foreach (var section in sections.Take(3))
        {
            this.SceneSections.Children.Add(section);
        }

        this.SceneSections.Children.Add(this.PostProcessingHeading);
        foreach (var section in sections.Skip(3))
        {
            this.SceneSections.Children.Add(section);
        }

        this.AddDisclosure(this.SkyAtmosphereSection, "Planet & ground", "Planet Radius", "Atmosphere Height", "Ground Albedo");
        this.AddDisclosure(this.SkyAtmosphereSection, "Scattering", "Rayleigh Height", "Mie Height", "Mie Anisotropy");
        this.AddDisclosure(this.SkyAtmosphereSection, "Aerial perspective", "Distance scale", "Scattering strength", "Start distance", "Height fog contribution");
        this.AddDisclosure(this.ExposureSection, "Metering & limits", ["Metering", "Auto Min EV", "Auto Max EV", "Target Luminance", "Spot Radius"], expanded: true);
        this.AddDisclosure(this.ExposureSection, "Adaptation", "Adapt Up", "Adapt Down", "Adaptation transition distance · Proposed");
        this.AddDisclosure(this.ExposureSection, "Histogram & calibration", "Key", "Low Percentile", "High Percentile", "Min Log Luminance", "Log Luminance Range", "Dark-sample influence · Proposed");
        this.AddDisclosure(this.ExposureSection, "Exposure shaping", "Metering mask · Proposed", "Exposure-compensation curve · Proposed");
    }

    private void GroundAlbedoPicker_ColorChanged(ColorPicker sender, ColorChangedEventArgs args)
    {
        if (this.ViewModel is { } model && InspectorRgbPresentation.ToDisplayColor(model.GroundAlbedoColor) != args.NewColor)
        {
            InspectorColorGestures.Apply(sender, owner => ((EnvironmentViewModel)owner).SetGroundAlbedoColor(InspectorRgbPresentation.ToLinearRgb(args.NewColor)));
        }
    }

    private void AddDisclosure(EditorPropertiesExpander section, string title, params string[] properties)
        => this.AddDisclosure(section, title, properties, expanded: false);

    private void AddDisclosure(EditorPropertiesExpander section, string title, string[] properties, bool expanded)
    {
        var cards = properties.Select(name => this.sectionCards[section].Single(card => string.Equals(card.PropertyName, name, StringComparison.Ordinal))).ToArray();
        var content = new StackPanel { Spacing = 4 };
        foreach (var card in cards)
        {
            section.Items.Remove(card);
            content.Children.Add(card);
        }

        var disclosure = new Expander
        {
            Style = (Style)this.Resources["QuietDisclosure"],
            Header = title,
            Content = content,
            IsExpanded = expanded,
            HorizontalAlignment = HorizontalAlignment.Stretch,
            HorizontalContentAlignment = HorizontalAlignment.Stretch,
        };
        this.disclosureCards[disclosure] = cards;
        section.Items.Add(disclosure);
    }

    private void OnLoaded(object sender, RoutedEventArgs args)
    {
        this.observedModel = this.ViewModel;
        if (this.observedModel is { } model)
        {
            model.FieldFocusRequested += this.OnFieldFocusRequested;
            model.PropertyChanged += this.OnEnvironmentPropertyChanged;
            this.FocusPendingField();
            this.ApplyScenePropertyFilter();
        }
    }

    private void OnUnloaded(object sender, RoutedEventArgs args)
    {
        if (this.observedModel is { } model)
        {
            model.FieldFocusRequested -= this.OnFieldFocusRequested;
            model.PropertyChanged -= this.OnEnvironmentPropertyChanged;
            this.observedModel = null;
        }
    }

    private void OnEnvironmentPropertyChanged(object? sender, System.ComponentModel.PropertyChangedEventArgs args)
    {
        if (args.PropertyName is nameof(EnvironmentViewModel.ExposureMode) or nameof(EnvironmentViewModel.ToneMapping) or nameof(EnvironmentViewModel.AutoExposureMeteringMode))
        {
            this.ApplyScenePropertyFilter();
        }
    }

    private void ClearScenePropertySearch_Click(object sender, RoutedEventArgs args)
        => this.ScenePropertySearchBox.Text = string.Empty;

    private async void ResetAtmosphereSources_Click(object sender, RoutedEventArgs args)
    {
        if (this.ViewModel is { } model)
        {
            await model.ResetAtmosphereSourcesAsync().ConfigureAwait(true);
        }
    }

    private async void ResetBackground_Click(object sender, RoutedEventArgs args)
    {
        if (this.ViewModel is { } model)
        {
            await model.ResetBackgroundAsync().ConfigureAwait(true);
        }
    }

    private void ClearMeteringMask_Click(object sender, RoutedEventArgs args)
        => this.ViewModel?.SetMeteringMask(null);

    private void MeteringMaskItem_Click(object sender, RoutedEventArgs args)
    {
        if (sender is FrameworkElement { DataContext: AssetPickerRow row } && row.Item.IsEnabled)
        {
            this.ViewModel?.SetMeteringMask(row.Item.Uri);
            this.MeteringMaskFlyout.Hide();
        }
    }

    private async void InspectPrimarySource_Click(object sender, RoutedEventArgs args)
    {
        if (this.ViewModel is { } model)
        {
            await model.InspectAtmosphereSourceAsync(model.SelectedSun).ConfigureAwait(true);
        }
    }

    private async void InspectSecondarySource_Click(object sender, RoutedEventArgs args)
    {
        if (this.ViewModel is { } model)
        {
            await model.InspectAtmosphereSourceAsync(model.SelectedSecondarySun).ConfigureAwait(true);
        }
    }

    private void RemoveExposureCurveKey_Click(object sender, RoutedEventArgs args)
    {
        if (sender is FrameworkElement { DataContext: ExposureCompensationKeyViewModel key })
        {
            this.ViewModel?.RemoveExposureCurveKeyCommand.Execute(key);
        }
    }

    private void ExposureCurveEditStarted(object? sender, NumberBoxEditSessionEventArgs args)
        => this.ViewModel?.BeginEditSession("AutoExposureCompensationCurve", args.InteractionKind);

    private void ExposureCurveEditCompleted(object? sender, NumberBoxEditSessionEventArgs args)
        => this.ViewModel?.CompleteEditSession(args);

    private void ExposureCurveKey_Validate(object? sender, ValidationEventArgs<float> args)
    {
        args.IsValid = sender is FrameworkElement { DataContext: ExposureCompensationKeyViewModel key, Tag: string field }
            && this.ViewModel?.ValidateExposureCurveKey(key, editMeteredEv: string.Equals(field, "MeteredEv", StringComparison.Ordinal), args.NewValue) == true;
    }

    private void ScenePropertyScope_Changed(object sender, SelectionChangedEventArgs args)
    {
        if (sender is CommunityToolkit.WinUI.Controls.Segmented selector)
        {
            this.propertyScope = selector.SelectedIndex switch { 1 => "Environment", 2 => "Post-processing", _ => "All" };
            this.ApplyScenePropertyFilter();
        }
    }

    private void ApplyScenePropertyFilter()
    {
        if (this.ViewModel is not { } model || this.ScenePropertySearchBox is null || this.sectionCards.Count == 0)
        {
            return;
        }

        var query = this.ScenePropertySearchBox.Text.Trim();
        var queryTerms = query.Split([' ', '\t', '/', '_', '-'], StringSplitOptions.RemoveEmptyEntries);
        var isSearching = queryTerms.Length > 0;
        this.ClearPropertySearchButton.Visibility = isSearching ? Visibility.Visible : Visibility.Collapsed;
        var sections = new[]
        {
            this.SkyAtmosphereSection,
            this.AtmosphereLightsSection,
            this.ExposureSection,
            this.ToneMappingSection,
            this.ColorGradingSection,
            this.BloomSection,
            this.BackgroundSection,
        };

        if (isSearching && !this.hasActiveSearch)
        {
            this.expandedBeforeSearch.Clear();
            foreach (var section in sections)
            {
                this.expandedBeforeSearch[section] = section.IsExpanded;
            }

            foreach (var disclosure in this.disclosureCards.Keys.Concat([this.PrimarySourceDisclosure, this.SecondarySourceDisclosure]))
            {
                this.disclosuresBeforeSearch[disclosure] = disclosure.IsExpanded;
            }
        }
        else if (!isSearching && this.hasActiveSearch)
        {
            foreach (var (section, wasExpanded) in this.expandedBeforeSearch)
            {
                section.IsExpanded = wasExpanded;
            }

            this.expandedBeforeSearch.Clear();
            foreach (var (disclosure, wasExpanded) in this.disclosuresBeforeSearch)
            {
                disclosure.IsExpanded = wasExpanded;
            }

            this.disclosuresBeforeSearch.Clear();
        }

        this.hasActiveSearch = isSearching;
        var visibleCardCount = 0;
        foreach (var section in sections)
        {
            var tags = (section.Tag as string ?? string.Empty).Split(';', StringSplitOptions.RemoveEmptyEntries);
            var scopeMatches = string.Equals(this.propertyScope, "All", StringComparison.Ordinal)
                || tags.Contains(this.propertyScope, StringComparer.Ordinal);
            var sectionApplicable = !tags.Contains("ToneMapping", StringComparer.Ordinal) || model.IsToneMappingControlsVisible;
            var sectionVisibleCount = 0;

            foreach (var card in this.sectionCards[section])
            {
                var applicable = sectionApplicable && IsPropertyApplicable(card, model);
                var matches = !isSearching || this.MatchesSearch(card, section, queryTerms);
                var visible = scopeMatches && (isSearching ? matches : applicable);
                card.Visibility = visible ? Visibility.Visible : Visibility.Collapsed;
                if (visible)
                {
                    sectionVisibleCount++;
                    visibleCardCount++;
                }

                var applicabilityMessage = isSearching && matches && !applicable
                    ? this.GetApplicabilityMessage(card, tags, model)
                    : null;
                this.SetApplicabilityNote(card, applicabilityMessage);
            }

            section.Visibility = scopeMatches && sectionVisibleCount > 0 ? Visibility.Visible : Visibility.Collapsed;
            if (isSearching && scopeMatches && sectionVisibleCount > 0)
            {
                section.IsExpanded = true;
            }
        }

        foreach (var (disclosure, cards) in this.disclosureCards)
        {
            var visible = cards.Any(card => card.Visibility == Visibility.Visible);
            disclosure.Visibility = visible ? Visibility.Visible : Visibility.Collapsed;
            if (isSearching && visible)
            {
                disclosure.IsExpanded = true;
            }
        }

        if (isSearching && this.AtmosphereLightsSection.Visibility == Visibility.Visible)
        {
            this.PrimarySourceDisclosure.IsExpanded = true;
            this.SecondarySourceDisclosure.IsExpanded = true;
        }

        this.EnvironmentHeading.Visibility = sections.Where(section => string.Equals(section.Tag as string, "Environment", StringComparison.Ordinal)).Any(section => section.Visibility == Visibility.Visible) ? Visibility.Visible : Visibility.Collapsed;
        this.PostProcessingHeading.Visibility = sections.Where(section => (section.Tag as string ?? string.Empty).Contains("Post-processing", StringComparison.Ordinal)).Any(section => section.Visibility == Visibility.Visible) ? Visibility.Visible : Visibility.Collapsed;
        this.NoScenePropertyMatches.Visibility = isSearching && visibleCardCount == 0 ? Visibility.Visible : Visibility.Collapsed;
    }

    private static bool IsPropertyApplicable(EditorPropertyCard card, EnvironmentViewModel model)
        => (card.Tag as string) switch
        {
            "AutoExposure" => model.IsAutoExposureVisible,
            "AutoExposureSpot" => model.IsAutoExposureVisible && model.AutoExposureMeteringMode == MeteringMode.Spot,
            "ManualExposure" => model.ExposureMode == Oxygen.Editor.World.Serialization.ExposureMode.Manual,
            _ => true,
        };

    private string? GetApplicabilityMessage(EditorPropertyCard card, string[] sectionTags, EnvironmentViewModel model)
    {
        if (card.Tag as string == "AutoExposureSpot")
        {
            return "Stored value; applies in Auto exposure mode with Spot metering.";
        }

        if (card.Tag as string == "AutoExposure")
        {
            return $"Stored value; applies in Auto exposure mode. Current mode: {model.ExposureMode}.";
        }

        if (card.Tag as string == "ManualExposure")
        {
            return $"Stored value; applies in Manual exposure mode. Current mode: {model.ExposureMode}.";
        }

        return sectionTags.Contains("ToneMapping", StringComparer.Ordinal)
            ? "Stored value; color grading is inactive while tone mapping is set to None."
            : null;
    }

    private void SetApplicabilityNote(EditorPropertyCard card, string? message)
    {
        if (!this.applicabilityNotes.TryGetValue(card, out var note))
        {
            note = new TextBlock
            {
                FontSize = 12,
                Foreground = card.Foreground,
                TextWrapping = TextWrapping.Wrap,
                Visibility = Visibility.Collapsed,
            };

            if (card.Content is StackPanel stack)
            {
                stack.Children.Add(note);
            }
            else if (card.Content is UIElement content)
            {
                var wrapper = new StackPanel { Spacing = 4 };
                card.Content = null;
                wrapper.Children.Add(content);
                wrapper.Children.Add(note);
                card.Content = wrapper;
            }

            this.applicabilityNotes[card] = note;
        }

        note.Text = message ?? string.Empty;
        note.Visibility = string.IsNullOrEmpty(message) ? Visibility.Collapsed : Visibility.Visible;
    }

    private bool MatchesSearch(EditorPropertyCard card, EditorPropertiesExpander section, IReadOnlyList<string> queryTerms)
    {
        if (this.propertySearchText.TryGetValue(card, out var searchableText))
        {
            return queryTerms.All(term => searchableText.Contains(NormalizeSearchText(term), StringComparison.Ordinal));
        }

        var values = new List<string>
        {
            card.PropertyName,
            card.Tag?.ToString() ?? string.Empty,
            section.Header?.ToString() ?? string.Empty,
            section.Description?.ToString() ?? string.Empty,
        };
        if (card.Content is DependencyObject content)
        {
            this.CollectSearchTerms(content, values);
        }

        searchableText = NormalizeSearchText(string.Join(' ', values));
        this.propertySearchText.Add(card, searchableText);
        return queryTerms.All(term => searchableText.Contains(NormalizeSearchText(term), StringComparison.Ordinal));
    }

    private void CollectSearchTerms(DependencyObject element, ICollection<string> values)
    {
        if (element is TextBlock note && this.applicabilityNotes.Values.Contains(note))
        {
            return;
        }

        if (element is FrameworkElement frameworkElement)
        {
            if (frameworkElement.Tag is { } tag)
            {
                values.Add(tag.ToString() ?? string.Empty);
            }

            if (ToolTipService.GetToolTip(frameworkElement) is string toolTip)
            {
                values.Add(toolTip);
            }
        }

        switch (element)
        {
            case NumberBox number:
                values.Add(number.Label);
                break;
            case VectorBox vector:
                values.Add(vector.Label);
                break;
            case TextBlock textBlock:
                values.Add(textBlock.Text);
                break;
            case ComboBox comboBox:
                values.Add(comboBox.SelectedItem?.ToString() ?? string.Empty);
                break;
        }

        if (element is Panel panel)
        {
            foreach (var child in panel.Children)
            {
                this.CollectSearchTerms(child, values);
            }
        }
        else if (element is Border border && border.Child is { } child)
        {
            this.CollectSearchTerms(child, values);
        }
        else if (element is ContentControl contentControl && contentControl.Content is DependencyObject content)
        {
            this.CollectSearchTerms(content, values);
        }
    }

    private static string NormalizeSearchText(string value)
        => string.Concat(value.Where(char.IsLetterOrDigit)).ToLowerInvariant();

    private void OnFieldFocusRequested(object? sender, EventArgs args) => this.FocusPendingField();

    private void FocusPendingField()
    {
        if (!string.Equals(this.observedModel?.PendingFieldFocus, Oxygen.Editor.Schemas.SceneEnvironmentConstraints.AerialStartPropertyPath, StringComparison.Ordinal))
        {
            return;
        }

        this.ScenePropertySearchBox.Text = string.Empty;
        this.ScenePropertyScopeSelector.SelectedIndex = 0;
        this.ApplyScenePropertyFilter();
        foreach (var (disclosure, cards) in this.disclosureCards)
        {
            if (cards.Contains(this.AerialStartCard))
            {
                disclosure.IsExpanded = true;
                _ = this.SkyAtmosphereSection.BringItemIntoView(disclosure);
            }
        }
        this.TryFocusAerialStart();
        _ = this.DispatcherQueue.TryEnqueue(this.TryFocusAerialStart);
    }

    private void TryFocusAerialStart()
    {
        if (this.IsLoaded && this.AerialStartInput.IsLoaded && this.observedModel is { } model
            && string.Equals(model.PendingFieldFocus, Oxygen.Editor.Schemas.SceneEnvironmentConstraints.AerialStartPropertyPath, StringComparison.Ordinal))
        {
            this.AerialStartInput.StartBringIntoView();
            if (this.AerialStartInput.Focus(FocusState.Programmatic))
            {
                model.AcknowledgeFieldFocus();
            }
        }
    }

    private void BackgroundPicker_ColorChanged(ColorPicker sender, ColorChangedEventArgs args)
    {
        if (this.ViewModel is { } model && InspectorRgbPresentation.ToDisplayColor(model.BackgroundColor) != args.NewColor)
        {
            InspectorColorGestures.Apply(sender, owner => ((EnvironmentViewModel)owner).SetBackgroundColor(InspectorRgbPresentation.ToLinearRgb(args.NewColor)));
        }
    }

    private void NumberEditStarted(object? sender, NumberBoxEditSessionEventArgs args)
    {
        if (sender is FrameworkElement { Tag: string field })
        {
            this.ViewModel?.BeginEditSession(field, args.InteractionKind);
        }
    }

    private void NumberEditCompleted(object? sender, NumberBoxEditSessionEventArgs args)
        => this.ViewModel?.CompleteEditSession(args);

    private void AerialStartValidate(object? sender, ValidationEventArgs<float> args)
        => args.IsValid = this.ViewModel?.ValidateAerialStart(args.NewValue) == true;

    private void VectorEditStarted(object? sender, VectorBoxEditSessionEventArgs args)
    {
        if (sender is FrameworkElement { Tag: string field })
        {
            this.ViewModel?.BeginEditSession($"{field}.{args.Component}", args.InteractionKind);
        }
    }

    private void VectorEditCompleted(object? sender, VectorBoxEditSessionEventArgs args)
        => this.ViewModel?.CompleteEditSession(new(args.InteractionKind, args.CompletionKind));

    private void ColorPickerLoaded(object sender, RoutedEventArgs args)
    {
        if (sender is ColorPicker picker)
        {
            InspectorColorGestures.Attach(picker, this.ViewModel, picker.Tag as string ?? "BackgroundColor");
        }
    }
}
