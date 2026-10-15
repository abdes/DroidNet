// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Controls;
using DroidNet.Mvvm.Generators;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Documents;
using Oxygen.Editor.ContentBrowser.AssetIdentity;
using Oxygen.Editor.ContentPipeline;

namespace Oxygen.Editor.World.Inspector.Environment;

/// <summary>Shows the scene's Play section: its starting camera and starting input contexts.</summary>
[ViewModel(typeof(PlaySectionViewModel))]
public sealed partial class PlaySectionView
{
    /// <summary>Initializes a new instance of the <see cref="PlaySectionView"/> class.</summary>
    public PlaySectionView() => this.InitializeComponent();

    /// <summary>Expands the section and brings one of its fields into view with focus.</summary>
    /// <param name="propertyPath">A <see cref="ScenePlayPropertyPaths"/> path.</param>
    /// <returns>Whether the path names a field of this section.</returns>
    internal bool FocusField(string propertyPath)
    {
        FrameworkElement? field = propertyPath switch
        {
            ScenePlayPropertyPaths.StartingCamera => this.StartingCameraPicker,
            ScenePlayPropertyPaths.StartingInputContexts => this.StartingInputContexts,
            _ => null,
        };
        if (field is null)
        {
            return false;
        }

        this.Section.IsExpanded = true;
        field.StartBringIntoView();
        if (field is Control control)
        {
            _ = control.Focus(FocusState.Programmatic);
        }

        return true;
    }

    private static bool IsCommit(NumberBoxEditSessionEventArgs args) => args.CompletionKind == NumberBoxEditCompletionKind.Commit;

    private void OnAutomaticCameraClick(Hyperlink sender, HyperlinkClickEventArgs args)
    {
        _ = sender;
        _ = args;
        this.ViewModel?.InspectAutomaticCameraCommand.Execute(parameter: null);
    }

    private void OnContextCandidateClick(object sender, ItemClickEventArgs args)
    {
        _ = sender;
        this.AddContextFlyout.Hide();
        this.ViewModel?.AddContextCommand.Execute(args.ClickedItem as ContentBrowserAssetItem);
    }

    private void OnPriorityStarted(object? sender, NumberBoxEditSessionEventArgs args)
    {
        _ = args;
        if (sender is FrameworkElement { DataContext: StartingInputContextRowViewModel row })
        {
            row.BeginPrioritySession();
        }
    }

    private void OnPriorityCompleted(object? sender, NumberBoxEditSessionEventArgs args)
    {
        if (sender is FrameworkElement { DataContext: StartingInputContextRowViewModel row })
        {
            row.CompletePrioritySession(IsCommit(args));
        }
    }
}
