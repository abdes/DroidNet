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
    private readonly ComboBox faceSize;
    private readonly TextBlock detectedLayout;
    private readonly StackPanel cubeSettings;
    private readonly ComboBox intent;
    private readonly ComboBox colorSpace;
    private readonly ComboBox format;
    private ContentDialog? dialog;

    /// <summary>Initializes the image import form for the reviewed request.</summary>
    /// <param name="viewModel">The image import review state.</param>
    public TextureImportDialogView(TextureImportDialogViewModel viewModel)
    {
        this.viewModel = viewModel ?? throw new ArgumentNullException(nameof(viewModel));
        var content = new StackPanel { MaxWidth = 560, Spacing = 12 };
        content.Children.Add(new TextBlock { Text = viewModel.SourcePath, TextWrapping = TextWrapping.Wrap });

        var shape = new ComboBox { Header = "Shape", ItemsSource = viewModel.Shapes, SelectedItem = viewModel.Shape };
        shape.SelectionChanged += (_, _) => viewModel.Shape = shape.SelectedItem as string ?? string.Empty;
        content.Children.Add(shape);

        var layout = new ComboBox { Header = "Cube layout", ItemsSource = viewModel.CubeLayouts, SelectedItem = viewModel.SelectedCubeLayout };
        layout.SelectionChanged += (_, _) => viewModel.SelectedCubeLayout = layout.SelectedItem as string ?? string.Empty;
        this.faceSize = new ComboBox { Header = "Face size", ItemsSource = viewModel.FaceSizes, SelectedItem = viewModel.FaceSize };
        this.faceSize.SelectionChanged += (_, _) => viewModel.FaceSize = this.faceSize.SelectedItem is int size ? size : 0;
        var cubeRow = new Grid { ColumnSpacing = 8 };
        cubeRow.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });
        cubeRow.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });
        cubeRow.Children.Add(layout);
        Grid.SetColumn(this.faceSize, 1);
        cubeRow.Children.Add(this.faceSize);
        this.detectedLayout = new TextBlock { TextWrapping = TextWrapping.Wrap };
        this.cubeSettings = new StackPanel { Spacing = 4 };
        this.cubeSettings.Children.Add(cubeRow);
        this.cubeSettings.Children.Add(this.detectedLayout);
        content.Children.Add(this.cubeSettings);

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

        this.intent = new ComboBox { Header = "Intent", ItemsSource = viewModel.Intents, SelectedItem = viewModel.Intent };
        var intent = this.intent;
        intent.SelectionChanged += (_, _) => viewModel.Intent = intent.SelectedItem as string ?? string.Empty;
        this.colorSpace = new ComboBox { Header = "Color space", ItemsSource = viewModel.ColorSpaces, SelectedItem = viewModel.ColorSpace };
        var colorSpace = this.colorSpace;
        colorSpace.SelectionChanged += (_, _) => viewModel.ColorSpace = colorSpace.SelectedItem as string ?? string.Empty;
        var settings = new Grid { ColumnSpacing = 8 };
        settings.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });
        settings.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });
        settings.Children.Add(intent);
        Grid.SetColumn(colorSpace, 1);
        settings.Children.Add(colorSpace);
        content.Children.Add(settings);

        this.format = new ComboBox { Header = "Output format", ItemsSource = viewModel.Formats, SelectedItem = viewModel.Format };
        var format = this.format;
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
        if (string.Equals(args.PropertyName, nameof(TextureImportDialogViewModel.DestinationFolder)
, StringComparison.Ordinal) && !string.Equals(this.destination.Text, this.viewModel.DestinationFolder, StringComparison.Ordinal))
        {
            this.destination.Text = this.viewModel.DestinationFolder;
        }

        this.UpdateReview();
    }

    private void UpdateReview()
    {
        this.outputPath.Text = this.viewModel.OutputVirtualPath;
        this.errorText.Text = this.viewModel.Error;
        this.cubeSettings.Visibility = this.viewModel.IsCube ? Visibility.Visible : Visibility.Collapsed;
        this.faceSize.Visibility = this.viewModel.IsPanorama ? Visibility.Visible : Visibility.Collapsed;
        this.detectedLayout.Text = this.viewModel.DetectedLayoutText;

        // A shape change resets the defaults, which the combo boxes must show.
        this.intent.SelectedItem = this.viewModel.Intent;
        this.colorSpace.SelectedItem = this.viewModel.ColorSpace;
        this.format.SelectedItem = this.viewModel.Format;
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
