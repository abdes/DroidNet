// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Controls;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;

namespace Oxygen.Editor.SceneInspector;

/// <summary>Edits atmosphere-source properties on the referenced directional light.</summary>
public sealed partial class AtmosphereSourceView : UserControl
{
    /// <summary>Initializes a new instance of the <see cref="AtmosphereSourceView"/> class.</summary>
    public AtmosphereSourceView()
    {
        this.InitializeComponent();
    }

    private void NumberEditStarted(object? sender, NumberBoxEditSessionEventArgs args)
    {
        if (this.DataContext is DirectionalLightViewModel model && sender is FrameworkElement { Tag: string field })
        {
            model.BeginEditSession(field, args.InteractionKind);
        }
    }

    private void NumberEditCompleted(object? sender, NumberBoxEditSessionEventArgs args)
        => (this.DataContext as DirectionalLightViewModel)?.CompleteEditSession(args);

    private void ColorEditStarted(object? sender, ColorBoxEditSessionEventArgs args)
        => InspectorColorEdits.Begin(this.DataContext as DirectionalLightViewModel, sender, args);

    private void ColorEditCompleted(object? sender, ColorBoxEditSessionEventArgs args)
        => InspectorColorEdits.Complete(this.DataContext as DirectionalLightViewModel, sender, args);
}
