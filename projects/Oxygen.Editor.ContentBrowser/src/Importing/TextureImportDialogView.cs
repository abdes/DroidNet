// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.ComponentModel;
using CommunityToolkit.WinUI;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;

namespace Oxygen.Editor.ContentBrowser.Importing;

/// <summary>Programmatic review content for standalone image texture import.</summary>
public sealed class TextureImportDialogView : UserControl
{
    private readonly TextureImportDialogViewModel viewModel;
    private readonly TextBox destination;
    private readonly TextBlock outputPath;
    private readonly TextBlock errorText;
    private ContentDialog? dialog;

    /// <summary>Initializes the image import form for the reviewed request.</summary>
    /// <param name="viewModel">The image import review state.</param>
    public TextureImportDialogView(TextureImportDialogViewModel viewModel)
    {
        this.viewModel = viewModel ?? throw new ArgumentNullException(nameof(viewModel));
        var content = new StackPanel { MaxWidth = 560, Spacing = 12 };
        content.Children.Add(new TextBlock { Text = viewModel.SourcePath, TextWrapping = TextWrapping.Wrap });

        var name = new TextBox { Header = "Texture name", Text = viewModel.Name };
        name.TextChanged += (_, _) => viewModel.Name = name.Text;
        content.Children.Add(name);

        this.destination = new TextBox { Header = "Authoring folder", Text = viewModel.DestinationFolder };
        this.destination.TextChanged += (_, _) => viewModel.DestinationFolder = this.destination.Text;
        var browse = new Button { Content = "Browse", VerticalAlignment = VerticalAlignment.Bottom };
        browse.Click += async (_, _) => await viewModel.BrowseDestinationCommand.ExecuteAsync(parameter: null).ConfigureAwait(true);
        var destinationRow = new Grid { ColumnSpacing = 8 };
        destinationRow.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });
        destinationRow.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });
        destinationRow.Children.Add(this.destination);
        Grid.SetColumn(browse, 1);
        destinationRow.Children.Add(browse);
        content.Children.Add(destinationRow);

        var intent = new ComboBox { Header = "Intent", ItemsSource = viewModel.Intents, SelectedItem = viewModel.Intent };
        intent.SelectionChanged += (_, _) => viewModel.Intent = intent.SelectedItem as string ?? string.Empty;
        var colorSpace = new ComboBox { Header = "Color space", ItemsSource = viewModel.ColorSpaces, SelectedItem = viewModel.ColorSpace };
        colorSpace.SelectionChanged += (_, _) => viewModel.ColorSpace = colorSpace.SelectedItem as string ?? string.Empty;
        var settings = new Grid { ColumnSpacing = 8 };
        settings.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });
        settings.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });
        settings.Children.Add(intent);
        Grid.SetColumn(colorSpace, 1);
        settings.Children.Add(colorSpace);
        content.Children.Add(settings);

        var format = new ComboBox { Header = "Output format", ItemsSource = viewModel.Formats, SelectedItem = viewModel.Format };
        format.SelectionChanged += (_, _) => viewModel.Format = format.SelectedItem as string ?? string.Empty;
        content.Children.Add(format);

        this.outputPath = new TextBlock { TextWrapping = TextWrapping.Wrap };
        this.errorText = new TextBlock { TextWrapping = TextWrapping.Wrap };
        content.Children.Add(this.outputPath);
        content.Children.Add(this.errorText);
        this.Content = content;
        this.Loaded += this.OnLoaded;
        this.Unloaded += this.OnUnloaded;
        this.UpdateReview();
    }

    private void OnLoaded(object sender, RoutedEventArgs args)
    {
        _ = sender;
        _ = args;
        this.dialog = this.FindAscendant<ContentDialog>();
        this.viewModel.PropertyChanged += this.OnViewModelChanged;
        this.UpdateReview();
    }

    private void OnUnloaded(object sender, RoutedEventArgs args)
    {
        _ = sender;
        _ = args;
        this.viewModel.PropertyChanged -= this.OnViewModelChanged;
        this.dialog = null;
    }

    private void OnViewModelChanged(object? sender, PropertyChangedEventArgs args)
    {
        _ = sender;
        if (args.PropertyName == nameof(TextureImportDialogViewModel.DestinationFolder)
            && !string.Equals(this.destination.Text, this.viewModel.DestinationFolder, StringComparison.Ordinal))
        {
            this.destination.Text = this.viewModel.DestinationFolder;
        }

        this.UpdateReview();
    }

    private void UpdateReview()
    {
        this.outputPath.Text = this.viewModel.OutputVirtualPath;
        this.errorText.Text = this.viewModel.Error;
        if (this.dialog is null && this.IsLoaded)
        {
            this.dialog = this.FindAscendant<ContentDialog>();
        }

        if (this.dialog is { } dialog)
        {
            dialog.IsPrimaryButtonEnabled = this.viewModel.CanAccept;
        }
    }
}
