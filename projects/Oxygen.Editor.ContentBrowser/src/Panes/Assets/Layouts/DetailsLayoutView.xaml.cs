// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Mvvm;
using DroidNet.Mvvm.Generators;

namespace Oxygen.Editor.ContentBrowser.Panes.Assets.Layouts;

/// <summary>
/// Represents the view for displaying assets in a table with sortable columns.
/// </summary>
[ViewModel(typeof(DetailsLayoutViewModel))]
public sealed partial class DetailsLayoutView
{
    private readonly AssetSelectionSync selection;

    /// <summary>
    /// Initializes a new instance of the <see cref="DetailsLayoutView"/> class.
    /// </summary>
    public DetailsLayoutView()
    {
        this.InitializeComponent();
        this.selection = new AssetSelectionSync(this.AssetTable);
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

    /// <summary>Returns the sort arrow for a column header, empty when the column does not order the results.</summary>
    /// <param name="field">The current sort field.</param>
    /// <param name="descending">Whether the order is descending.</param>
    /// <param name="column">The header's field name.</param>
    /// <returns>The arrow glyph, or an empty string.</returns>
    internal static string SortGlyph(AssetSortField field, bool descending, string column)
        => !string.Equals(field.ToString(), column, StringComparison.Ordinal) ? string.Empty : descending ? "" : "";

    private void OnViewModelChanged(object? sender, ViewModelChangedEventArgs<DetailsLayoutViewModel> args)
    {
        args.OldValue?.SelectionRevealRequested -= this.OnSelectionRevealRequested;
        this.Attach(this.ViewModel);
    }

    private void Attach(DetailsLayoutViewModel? model)
    {
        if (model is null)
        {
            return;
        }

        model.SelectionRevealRequested += this.OnSelectionRevealRequested;
        this.selection.Model = model;
        AssetSelectionReveal.Apply(this.AssetTable, model);
    }

    private void OnSelectionRevealRequested(object? sender, EventArgs args)
    {
        if (this.ViewModel is { } model)
        {
            AssetSelectionReveal.Apply(this.AssetTable, model);
        }
    }
}
