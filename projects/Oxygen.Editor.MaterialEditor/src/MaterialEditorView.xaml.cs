// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Controls;
using DroidNet.Mvvm.Generators;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Input;

namespace Oxygen.Editor.MaterialEditor;

/// <summary>
/// Material editor document view.
/// </summary>
[ViewModel(typeof(MaterialEditorViewModel))]
public sealed partial class MaterialEditorView
{
    private MaterialEditorViewModel? pickOwner;
    private MaterialEditorViewModel? statusOwner;

    /// <summary>
    /// Initializes a new instance of the <see cref="MaterialEditorView"/> class.
    /// </summary>
    public MaterialEditorView()
    {
        this.InitializeComponent();
        this.Loaded += (_, _) =>
        {
            this.ObserveCookStatus();
            this.ViewModel?.Activate();
        };
        this.Unloaded += (_, _) =>
        {
            this.StopObservingCookStatus();
            this.ViewModel?.Deactivate();
        };
        this.ViewModelChanged += (_, _) =>
        {
            if (this.IsLoaded)
            {
                this.ObserveCookStatus();
            }
        };
    }

    private void ObserveCookStatus()
    {
        this.StopObservingCookStatus();
        this.statusOwner = this.ViewModel;
        this.statusOwner?.PropertyChanged += this.OnCookStatusChanged;

        _ = VisualStateManager.GoToState(this, this.statusOwner?.CookStatusTone ?? "Neutral", useTransitions: false);
    }

    private void MaterialHeader_SizeChanged(object sender, SizeChangedEventArgs args)
    {
        // Keep the title/status together; move the unchanged actions only when their row is too narrow.
        var actionRow = args.NewSize.Width < 480 ? 1 : 0;
        if (Grid.GetRow(this.MaterialActions) == actionRow)
        {
            return;
        }

        Grid.SetRow(this.MaterialActions, actionRow);
        Grid.SetColumn(this.MaterialActions, actionRow == 0 ? 2 : 0);
        Grid.SetColumnSpan(this.MaterialActions, actionRow == 0 ? 1 : 3);
        Grid.SetColumnSpan(this.MaterialIdentity, actionRow == 0 ? 1 : 2);
        this.MaterialActions.Margin = new Thickness(0, actionRow == 0 ? 0 : 8, 0, 0);
    }

    private void StopObservingCookStatus()
    {
        this.statusOwner?.PropertyChanged -= this.OnCookStatusChanged;
        this.statusOwner = null;
    }

    private void OnCookStatusChanged(object? sender, System.ComponentModel.PropertyChangedEventArgs args)
    {
        if (string.Equals(args.PropertyName, nameof(MaterialEditorViewModel.CookStatusTone), StringComparison.Ordinal))
        {
            _ = VisualStateManager.GoToState(this, this.ViewModel?.CookStatusTone ?? "Neutral", useTransitions: false);
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

    private void ColorEditStarted(object? sender, ColorBoxEditSessionEventArgs args)
    {
        if (this.ViewModel is not { } model)
        {
            return;
        }

        if (args.Channel is { } channel)
        {
            model.BeginEditSession("BaseColor" + ColorBox.ChannelName(channel), args.InteractionKind);
            return;
        }

        // A pick belongs to the document it began on; a later document never receives its colors or its end.
        this.pickOwner = model;
        model.BeginEditSession("Base color", args.InteractionKind);
    }

    private void ColorEditCompleted(object? sender, ColorBoxEditSessionEventArgs args)
    {
        if (args.Channel is not null)
        {
            this.ViewModel?.CompleteEditSession(new NumberBoxEditSessionEventArgs(args.InteractionKind, args.CompletionKind, args.InputText));
            return;
        }

        var owner = this.pickOwner;
        this.pickOwner = null;
        if (owner is not null && ReferenceEquals(owner, this.ViewModel))
        {
            owner.EndEditSession(args.CompletionKind ?? NumberBoxEditCompletionKind.Commit);
        }
    }

    private void OnBaseColorPicked(object? sender, ColorBoxColorPickedEventArgs args)
    {
        args.Handled = true;
        if (this.pickOwner is { } owner && ReferenceEquals(owner, this.ViewModel))
        {
            owner.SetBaseColor(args.Color);
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
