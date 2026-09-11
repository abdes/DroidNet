// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Controls;
using DroidNet.Mvvm.Generators;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;

namespace Oxygen.Editor.World.Inspector;

/// <summary>
/// Scene environment inspector view.
/// </summary>
[ViewModel(typeof(EnvironmentViewModel))]
public sealed partial class EnvironmentView
{
    private EnvironmentViewModel? observedModel;

    /// <summary>
    /// Initializes a new instance of the <see cref="EnvironmentView"/> class.
    /// </summary>
    public EnvironmentView()
    {
        this.InitializeComponent();
        this.Loaded += this.OnLoaded;
        this.Unloaded += this.OnUnloaded;
        this.AerialStartInput.Loaded += (_, _) => this.TryFocusAerialStart();
    }

    private void OnLoaded(object sender, RoutedEventArgs args)
    {
        this.observedModel = this.ViewModel;
        if (this.observedModel is { } model)
        {
            model.FieldFocusRequested += this.OnFieldFocusRequested;
            this.FocusPendingField();
        }
    }

    private void OnUnloaded(object sender, RoutedEventArgs args)
    {
        if (this.observedModel is { } model)
        {
            model.FieldFocusRequested -= this.OnFieldFocusRequested;
            this.observedModel = null;
        }
    }

    private void OnFieldFocusRequested(object? sender, EventArgs args) => this.FocusPendingField();

    private void FocusPendingField()
    {
        if (!string.Equals(this.observedModel?.PendingFieldFocus, Oxygen.Editor.Schemas.SceneEnvironmentConstraints.AerialStartPropertyPath, StringComparison.Ordinal))
        {
            return;
        }

        _ = this.SkyAtmosphereSection.BringItemIntoView(this.AerialStartCard);
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
        if (this.ViewModel is { } model && model.BackgroundColor != args.NewColor)
        {
            InspectorColorGestures.Apply(sender, owner => ((EnvironmentViewModel)owner).SetBackgroundColor(args.NewColor));
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
            InspectorColorGestures.Attach(picker, this.ViewModel, "BackgroundColor");
        }
    }
}
