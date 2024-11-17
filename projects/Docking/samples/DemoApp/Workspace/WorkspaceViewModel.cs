// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.Mvvm.ComponentModel;
using DroidNet.Docking.Layouts.GridFlow;
using Microsoft.UI.Xaml;

namespace DroidNet.Docking.Demo.Workspace;

/// <summary>
/// The ViewModel for the docking workspace.
/// </summary>
/// <remarks>
/// <para>
/// The <see cref="WorkspaceViewModel"/> class is responsible for managing the state and behavior of the docking workspace.
/// It initializes the workspace content and updates it based on layout changes.
/// </para>
/// <para>
/// This ViewModel uses dependency injection to resolve required services and view models, ensuring a decoupled and testable design.
/// </para>
/// </remarks>
/// <example>
/// <para>
/// To create an instance of <see cref="WorkspaceViewModel"/> and bind it to a view, use the following code:
/// </para>
/// <code><![CDATA[
/// var docker = new CustomDocker();
/// var layout = new GridFlowLayout(new CustomDockViewFactory());
/// var workspaceViewModel = new WorkspaceViewModel(docker, layout);
/// ]]></code>
/// </example>
[System.Diagnostics.CodeAnalysis.SuppressMessage("Maintainability", "CA1515:Consider making public types internal", Justification = "ViewModel must be public because the 'ViewModel' property in the View is public")]
public partial class WorkspaceViewModel : ObservableObject
{
    /// <summary>
    /// Gets or sets the workspace content.
    /// </summary>
    /// <value>
    /// The <see cref="UIElement"/> representing the workspace content.
    /// </value>
    [ObservableProperty]
    private UIElement? workspaceContent;

    /// <summary>
    /// Initializes a new instance of the <see cref="WorkspaceViewModel"/> class.
    /// </summary>
    /// <param name="docker">The docker instance used to manage the docking operations within the workspace.</param>
    /// <param name="layout">The layout engine used to arrange the dockable entities within the workspace.</param>
    public WorkspaceViewModel(IDocker docker, GridFlowLayout layout)
    {
        this.UpdateContent(docker, layout);

        docker.LayoutChanged += (_, args) =>
        {
            if (args.Reason is LayoutChangeReason.Docking)
            {
                this.UpdateContent(docker, layout);
            }
        };
    }

    /// <summary>
    /// Updates the workspace content based on the current layout.
    /// </summary>
    /// <param name="docker">The docker instance used to manage the docking operations within the workspace.</param>
    /// <param name="layout">The layout engine used to arrange the dockable entities within the workspace.</param>
    /// <exception cref="InvalidOperationException">
    /// Thrown when the provided layout engine does not produce a <see cref="UIElement"/>.
    /// </exception>
    private void UpdateContent(IDocker docker, GridFlowLayout layout)
    {
        docker.Layout(layout);
        var content = layout.CurrentGrid;
        this.WorkspaceContent = content as UIElement ??
                                throw new InvalidOperationException(
                                    "the provided layout engine does not produce a UIElement");
    }
}
