// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.ComponentModel;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;

namespace Oxygen.Editor.SceneEditor.Play;

/// <summary>
/// The scene toolbar's Play transport: Play or Restart with the player launch settings, Stop or Cancel, and the
/// status. Its colors are visual states, set from <see cref="ScenePlayToolbarViewModel.PrimaryAccent"/>,
/// <see cref="ScenePlayToolbarViewModel.CanStop"/> and <see cref="ScenePlayToolbarViewModel.StatusTone"/>.
/// </summary>
public sealed partial class PlayTransportBar : UserControl
{
    /// <summary>Identifies the <see cref="ViewModel"/> dependency property.</summary>
    public static readonly DependencyProperty ViewModelProperty = DependencyProperty.Register(
        nameof(ViewModel),
        typeof(ScenePlayToolbarViewModel),
        typeof(PlayTransportBar),
        new PropertyMetadata(defaultValue: null, OnViewModelChanged));

    /// <summary>Initializes a new instance of the <see cref="PlayTransportBar"/> class.</summary>
    public PlayTransportBar()
    {
        this.InitializeComponent();
        this.Loaded += (_, _) => this.UpdateStates();
    }

    /// <summary>Gets or sets the Play transport of the scene.</summary>
    public ScenePlayToolbarViewModel? ViewModel
    {
        get => (ScenePlayToolbarViewModel?)this.GetValue(ViewModelProperty);
        set => this.SetValue(ViewModelProperty, value);
    }

    private static void OnViewModelChanged(DependencyObject sender, DependencyPropertyChangedEventArgs args)
    {
        var bar = (PlayTransportBar)sender;
        if (args.OldValue is ScenePlayToolbarViewModel previous)
        {
            previous.PropertyChanged -= bar.OnViewModelPropertyChanged;
        }

        if (args.NewValue is ScenePlayToolbarViewModel current)
        {
            current.PropertyChanged += bar.OnViewModelPropertyChanged;
        }

        bar.UpdateStates();
    }

    private void OnViewModelPropertyChanged(object? sender, PropertyChangedEventArgs args)
    {
        if (args.PropertyName is nameof(ScenePlayToolbarViewModel.PrimaryAccent)
            or nameof(ScenePlayToolbarViewModel.CanStop)
            or nameof(ScenePlayToolbarViewModel.StatusTone))
        {
            this.UpdateStates();
        }
    }

    private void UpdateStates()
    {
        if (!this.IsLoaded || this.ViewModel is not { } model)
        {
            return;
        }

        _ = VisualStateManager.GoToState(this, model.PrimaryAccent.ToString(), useTransitions: false);
        _ = VisualStateManager.GoToState(this, model.CanStop ? "StopActive" : "StopInactive", useTransitions: false);
        _ = VisualStateManager.GoToState(this, model.StatusTone.ToString(), useTransitions: false);
    }
}
