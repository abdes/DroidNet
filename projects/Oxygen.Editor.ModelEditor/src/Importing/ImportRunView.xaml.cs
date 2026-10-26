// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;

namespace Oxygen.Editor.ModelEditor.Importing;

/// <summary>The Import run flyout's content: the run's scheduling and each pipeline's workers and queue, each unset
/// value showing the ImportTool's default in secondary text.</summary>
public sealed partial class ImportRunView : UserControl
{
    /// <summary>Initializes a new instance of the <see cref="ImportRunView"/> class.</summary>
    /// <param name="viewModel">The run's settings.</param>
    public ImportRunView(ImportRunViewModel viewModel)
    {
        this.ViewModel = viewModel ?? throw new ArgumentNullException(nameof(viewModel));
        this.InitializeComponent();
    }

    /// <summary>Gets the run's settings.</summary>
    public ImportRunViewModel ViewModel { get; }

    /// <summary>Shows a row's override bar and reset while it sets either value.</summary>
    /// <param name="first">Whether the first value is set.</param>
    /// <param name="second">Whether the second value is set.</param>
    /// <returns>Visible when either is set.</returns>
    internal static Visibility ShownWhenEither(bool first, bool second) => first || second ? Visibility.Visible : Visibility.Collapsed;

    /// <summary>Gets an unset value's look: the default shows dimmed, as secondary text, until it is set.</summary>
    /// <param name="isSet">Whether the run sets the value.</param>
    /// <returns>The value's opacity.</returns>
    internal static double ValueOpacity(bool isSet) => isSet ? 1 : 0.6;

    /// <summary>Names a pipeline's field for assistive technology, such as "Textures Workers".</summary>
    /// <param name="pipeline">The pipeline's row header.</param>
    /// <param name="field">The field's column header.</param>
    /// <returns>The field's name.</returns>
    internal static string FieldName(string pipeline, string field) => $"{pipeline} {field}";

    private void OnResetPipeline(object sender, RoutedEventArgs args)
    {
        if (sender is FrameworkElement { DataContext: ImportRunPipeline pipeline })
        {
            pipeline.Workers.Value = null;
            pipeline.Queue.Value = null;
        }
    }
}
