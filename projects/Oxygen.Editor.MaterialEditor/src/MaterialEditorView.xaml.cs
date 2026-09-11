// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Controls;
using DroidNet.Mvvm.Generators;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Input;
using Windows.System;

namespace Oxygen.Editor.MaterialEditor;

/// <summary>
/// Material editor document view.
/// </summary>
[ViewModel(typeof(MaterialEditorViewModel))]
public sealed partial class MaterialEditorView
{
    private MaterialEditorViewModel? colorOwner;
    private bool colorGestureActive;

    /// <summary>
    /// Initializes a new instance of the <see cref="MaterialEditorView"/> class.
    /// </summary>
    public MaterialEditorView()
    {
        this.InitializeComponent();
        this.Loaded += (_, _) => this.ViewModel?.Activate();
        this.Unloaded += (_, _) => this.ViewModel?.Deactivate();
    }

    private void BaseColorPicker_ColorChanged(ColorPicker sender, ColorChangedEventArgs args)
    {
        if (ReferenceEquals(this.colorOwner, this.ViewModel))
        {
            this.BeginColorGesture(NumberBoxEditInteractionKind.PointerDrag);
            this.colorOwner?.SetBaseColor(args.NewColor);
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

    private void ColorPickerLoaded(object sender, RoutedEventArgs args)
    {
        this.colorOwner = this.ViewModel;
        var picker = (ColorPicker)sender;
        picker.AddHandler(PointerPressedEvent, new PointerEventHandler(this.ColorPressed), handledEventsToo: true);
        picker.AddHandler(PointerReleasedEvent, new PointerEventHandler(this.ColorReleased), handledEventsToo: true);
        picker.AddHandler(PointerCaptureLostEvent, new PointerEventHandler(this.ColorCaptureLost), handledEventsToo: true);
        picker.AddHandler(KeyDownEvent, new KeyEventHandler(this.ColorKeyDown), handledEventsToo: true);
    }

    private void ColorPickerUnloaded(object sender, RoutedEventArgs args)
    {
        this.EndColorGesture(NumberBoxEditCompletionKind.Commit);
        this.colorOwner = null;
        var picker = (ColorPicker)sender;
        picker.RemoveHandler(PointerPressedEvent, new PointerEventHandler(this.ColorPressed));
        picker.RemoveHandler(PointerReleasedEvent, new PointerEventHandler(this.ColorReleased));
        picker.RemoveHandler(PointerCaptureLostEvent, new PointerEventHandler(this.ColorCaptureLost));
        picker.RemoveHandler(KeyDownEvent, new KeyEventHandler(this.ColorKeyDown));
    }

    private void BeginColorGesture(NumberBoxEditInteractionKind interaction)
    {
        if (!this.colorGestureActive && this.colorOwner is not null && ReferenceEquals(this.colorOwner, this.ViewModel))
        {
            this.colorGestureActive = true;
            this.colorOwner.BeginEditSession("Base color", interaction);
        }
    }

    private void EndColorGesture(NumberBoxEditCompletionKind completion)
    {
        if (this.colorGestureActive)
        {
            this.colorGestureActive = false;
            this.colorOwner?.EndEditSession(completion);
        }
    }

    private void ColorPressed(object sender, PointerRoutedEventArgs args) => this.BeginColorGesture(NumberBoxEditInteractionKind.PointerDrag);

    private void ColorReleased(object sender, PointerRoutedEventArgs args) => this.EndColorGesture(NumberBoxEditCompletionKind.Commit);

    private void ColorCaptureLost(object sender, PointerRoutedEventArgs args) => this.EndColorGesture(NumberBoxEditCompletionKind.Cancel);

    private void ColorLostFocus(object sender, RoutedEventArgs args) => this.EndColorGesture(NumberBoxEditCompletionKind.Commit);

    private void ColorKeyDown(object sender, KeyRoutedEventArgs args)
    {
        if (args.Key is VirtualKey.Escape or VirtualKey.Enter)
        {
            this.EndColorGesture(args.Key == VirtualKey.Escape ? NumberBoxEditCompletionKind.Cancel : NumberBoxEditCompletionKind.Commit);
            args.Handled = true;
        }
        else
        {
            this.BeginColorGesture(NumberBoxEditInteractionKind.Text);
        }
    }

    private void UndoInvoked(KeyboardAccelerator sender, KeyboardAcceleratorInvokedEventArgs args)
    {
        if (this.ViewModel?.UndoCommand.CanExecute(parameter: null) == true)
        {
            this.ViewModel.UndoCommand.Execute(parameter: null);
            args.Handled = true;
        }
    }

    private void RedoInvoked(KeyboardAccelerator sender, KeyboardAcceleratorInvokedEventArgs args)
    {
        if (this.ViewModel?.RedoCommand.CanExecute(parameter: null) == true)
        {
            this.ViewModel.RedoCommand.Execute(parameter: null);
            args.Handled = true;
        }
    }
}
