// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.ComponentModel;
using DroidNet.Mvvm;
using DroidNet.Mvvm.Generators;
using Microsoft.UI.Xaml.Controls;

namespace Oxygen.Editor.ContentBrowser.Panes.Assets.Layouts;

/// <summary>
/// Represents the view for displaying assets in a tiles layout in the World Editor.
/// </summary>
[ViewModel(typeof(TilesLayoutViewModel))]
public sealed partial class TilesLayoutView
{
    // The name, type and status lines under the preview.
    private const double CaptionHeight = 66;

    private readonly AssetSelectionSync selection;

    /// <summary>
    /// Initializes a new instance of the <see cref="TilesLayoutView"/> class.
    /// </summary>
    public TilesLayoutView()
    {
        this.InitializeComponent();
        this.selection = new AssetSelectionSync(this.BasicGridView);
        this.Loaded += (_, _) =>
        {
            this.ViewModelChanged += this.OnViewModelChanged;
            this.Attach(this.ViewModel);
        };
        this.Unloaded += (_, _) =>
        {
            this.ViewModelChanged -= this.OnViewModelChanged;
            this.Detach(this.ViewModel);
            this.selection.Model = null;
        };
    }

    private void OnViewModelChanged(object? sender, ViewModelChangedEventArgs<TilesLayoutViewModel> args)
    {
        this.Detach(args.OldValue);
        this.Attach(this.ViewModel);
    }

    private void Attach(TilesLayoutViewModel? model)
    {
        if (model is null)
        {
            return;
        }

        model.SelectionRevealRequested += this.OnSelectionRevealRequested;
        model.Presentation.PropertyChanged += this.OnPresentationChanged;
        this.selection.Model = model;
        this.ApplyTileSize();
        AssetSelectionReveal.Apply(this.BasicGridView, model);
    }

    private void Detach(TilesLayoutViewModel? model)
    {
        if (model is null)
        {
            return;
        }

        model.SelectionRevealRequested -= this.OnSelectionRevealRequested;
        model.Presentation.PropertyChanged -= this.OnPresentationChanged;
    }

    private void OnSelectionRevealRequested(object? sender, EventArgs args)
    {
        if (this.ViewModel is { } model)
        {
            AssetSelectionReveal.Apply(this.BasicGridView, model);
        }
    }

    private void OnPresentationChanged(object? sender, PropertyChangedEventArgs args)
    {
        if (string.Equals(args.PropertyName, nameof(AssetBrowserPresentation.TileSize), StringComparison.Ordinal))
        {
            this.ApplyTileSize();
        }
    }

    // The preview keeps a 4:3 frame at every size; the caption keeps its height.
    private void ApplyTileSize()
    {
        if (this.ViewModel is not { } model)
        {
            return;
        }

        if (this.BasicGridView.ItemsPanelRoot is not ItemsWrapGrid panel)
        {
            // The panel is created on the first layout pass after the view loads.
            this.BasicGridView.LayoutUpdated -= this.OnFirstLayout;
            this.BasicGridView.LayoutUpdated += this.OnFirstLayout;
            return;
        }

        var size = model.Presentation.TileSize;
        panel.ItemWidth = size;
        panel.ItemHeight = Math.Round(size * 0.75) + CaptionHeight;
    }

    private void OnFirstLayout(object? sender, object args)
    {
        if (this.BasicGridView.ItemsPanelRoot is ItemsWrapGrid)
        {
            this.BasicGridView.LayoutUpdated -= this.OnFirstLayout;
            this.ApplyTileSize();
        }
    }
}
