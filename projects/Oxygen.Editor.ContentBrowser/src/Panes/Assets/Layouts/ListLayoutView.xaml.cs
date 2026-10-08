// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Mvvm;
using DroidNet.Mvvm.Generators;

namespace Oxygen.Editor.ContentBrowser.Panes.Assets.Layouts;

/// <summary>
/// Represents the view for displaying assets in a list layout in the World Editor.
/// </summary>
[ViewModel(typeof(ListLayoutViewModel))]
public sealed partial class ListLayoutView
{
    private readonly AssetSelectionSync selection;

    /// <summary>
    /// Initializes a new instance of the <see cref="ListLayoutView"/> class.
    /// </summary>
    public ListLayoutView()
    {
        this.InitializeComponent();
        this.selection = new AssetSelectionSync(this.AssetList);
        this.Loaded += (_, _) =>
        {
            this.ViewModelChanged += this.OnViewModelChanged;
            this.Attach(this.ViewModel);
        };
        this.Unloaded += (_, _) =>
        {
            this.ViewModelChanged -= this.OnViewModelChanged;
            this.ViewModel?.SelectionRevealRequested -= this.OnSelectionRevealRequested;
            this.selection.Model = null;
        };
    }

    private void OnViewModelChanged(object? sender, ViewModelChangedEventArgs<ListLayoutViewModel> args)
    {
        args.OldValue?.SelectionRevealRequested -= this.OnSelectionRevealRequested;
        this.Attach(this.ViewModel);
    }

    private void Attach(ListLayoutViewModel? model)
    {
        if (model is null)
        {
            return;
        }

        model.SelectionRevealRequested += this.OnSelectionRevealRequested;
        this.selection.Model = model;
        AssetSelectionReveal.Apply(this.AssetList, model);
    }

    private void OnSelectionRevealRequested(object? sender, EventArgs args)
    {
        if (this.ViewModel is { } model)
        {
            AssetSelectionReveal.Apply(this.AssetList, model);
        }
    }
}
