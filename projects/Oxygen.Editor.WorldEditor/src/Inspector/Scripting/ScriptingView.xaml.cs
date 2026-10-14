// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Controls;
using DroidNet.Mvvm.Generators;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Oxygen.Editor.World.SceneExplorer;

namespace Oxygen.Editor.World.Inspector.Scripting;

/// <summary>The Scripting section of the node inspector.</summary>
[ViewModel(typeof(ScriptingViewModel))]
public sealed partial class ScriptingView
{
    private Flyout? openNodeFlyout;

    /// <summary>Initializes a new instance of the <see cref="ScriptingView"/> class.</summary>
    public ScriptingView() => this.InitializeComponent();

    private static bool IsCommit(NumberBoxEditSessionEventArgs args) => args.CompletionKind == NumberBoxEditCompletionKind.Commit;

    private void OnFieldNumberStarted(object? sender, NumberBoxEditSessionEventArgs args)
    {
        _ = args;
        if (sender is FrameworkElement { DataContext: ScriptParameterFieldViewModel field })
        {
            field.BeginSession();
        }
    }

    private void OnFieldNumberCompleted(object? sender, NumberBoxEditSessionEventArgs args)
    {
        if (sender is FrameworkElement { DataContext: ScriptParameterFieldViewModel field })
        {
            field.CompleteSession(IsCommit(args));
        }
    }

    private void OnVectorStarted(object? sender, VectorBoxEditSessionEventArgs args)
    {
        _ = args;
        if (sender is FrameworkElement { DataContext: ScriptParameterFieldViewModel field })
        {
            field.BeginSession();
        }
    }

    private void OnVectorCompleted(object? sender, VectorBoxEditSessionEventArgs args)
    {
        if (sender is FrameworkElement { DataContext: ScriptParameterFieldViewModel field })
        {
            field.CompleteSession(args.CompletionKind == NumberBoxEditCompletionKind.Commit);
        }
    }

    // A channel edit or a pick is one session: the box sets the channels as the pick moves, and the color commits
    // once, when the session ends.
    private void OnColorStarted(object? sender, ColorBoxEditSessionEventArgs args)
    {
        _ = args;
        if (sender is FrameworkElement { DataContext: ScriptParameterFieldViewModel field })
        {
            field.BeginSession();
        }
    }

    private void OnColorCompleted(object? sender, ColorBoxEditSessionEventArgs args)
    {
        if (sender is FrameworkElement { DataContext: ScriptParameterFieldViewModel field })
        {
            field.CompleteSession(args.CompletionKind == NumberBoxEditCompletionKind.Commit);
        }
    }

    private void OnOrderStarted(object? sender, NumberBoxEditSessionEventArgs args)
    {
        _ = args;
        if (sender is FrameworkElement { DataContext: ScriptSlotCardViewModel card })
        {
            card.BeginOrderSession();
        }
    }

    private void OnOrderCompleted(object? sender, NumberBoxEditSessionEventArgs args)
    {
        if (sender is FrameworkElement { DataContext: ScriptSlotCardViewModel card })
        {
            card.CompleteOrderSession(IsCommit(args));
        }
    }

    private void OnNodeFlyoutOpening(object? sender, object args)
    {
        _ = args;
        if (sender is Flyout { Target.DataContext: ScriptParameterFieldViewModel field } flyout)
        {
            this.openNodeFlyout = flyout;
            field.NodeQuery = string.Empty;
            field.RefreshNodeChoices();
        }
    }

    private void OnNodeChosen(object sender, ItemClickEventArgs args)
    {
        if (sender is FrameworkElement { DataContext: ScriptParameterFieldViewModel field }
            && args.ClickedItem is ScriptNodeChoice node)
        {
            field.SelectNode(node);
            this.openNodeFlyout?.Hide();
            this.openNodeFlyout = null;
        }
    }

    private void OnNodeDragOver(object sender, DragEventArgs args)
    {
        if (SceneNodeDragData.Contains(args.DataView) && sender is FrameworkElement { DataContext: ScriptParameterFieldViewModel { CanEdit: true } })
        {
            // The tree moves or copies only on its own rows, so taking its requested operation changes nothing there.
            args.AcceptedOperation = args.DataView.RequestedOperation;
            args.DragUIOverride.Caption = "Reference this node";
            args.Handled = true;
        }
    }

    private async void OnNodeDrop(object sender, DragEventArgs args)
    {
        if (sender is not FrameworkElement { DataContext: ScriptParameterFieldViewModel field } || !SceneNodeDragData.Contains(args.DataView))
        {
            return;
        }

        args.Handled = true;
        var deferral = args.GetDeferral();
        try
        {
            _ = field.DropNodes(await SceneNodeDragData.ReadAsync(args.DataView).ConfigureAwait(true));
        }
        finally
        {
            deferral.Complete();
        }
    }
}
