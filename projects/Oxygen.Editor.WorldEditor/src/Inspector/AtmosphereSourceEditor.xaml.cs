// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Controls;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;

namespace Oxygen.Editor.World.Inspector;

/// <summary>Edits atmosphere-source properties on the referenced directional light.</summary>
public sealed partial class AtmosphereSourceView : UserControl
{
    /// <summary>Initializes a new instance of the <see cref="AtmosphereSourceView"/> class.</summary>
    public AtmosphereSourceView()
    {
        this.InitializeComponent();
        InspectorRgbPresentation.Configure(this.DiskMultiplier);
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

    private void VectorEditStarted(object? sender, VectorBoxEditSessionEventArgs args)
        => (this.DataContext as DirectionalLightViewModel)?.BeginEditSession($"DiskScale{args.Component}", args.InteractionKind);

    private void VectorEditCompleted(object? sender, VectorBoxEditSessionEventArgs args)
        => (this.DataContext as DirectionalLightViewModel)?.CompleteEditSession(new(args.InteractionKind, args.CompletionKind));
}
