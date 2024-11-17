// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.Mvvm.ComponentModel;
using DroidNet.Docking.Demo.Workspace;
using DroidNet.Docking.Layouts;
using DroidNet.Docking.Layouts.GridFlow;
using DryIoc;

namespace DroidNet.Docking.Demo.Shell;

/// <summary>
/// The ViewModel for the application's main window shell.
/// </summary>
/// <remarks>
/// <para>
/// The <see cref="ShellViewModel"/> class is responsible for managing the state and behavior of the main window shell.
/// It initializes the workspace and provides the necessary data bindings for the shell view.
/// </para>
/// <para>
/// This ViewModel uses dependency injection to resolve required services and view models, ensuring a decoupled and testable design.
/// </para>
/// </remarks>
/// <example>
/// <para>
/// To create an instance of <see cref="ShellViewModel"/> and bind it to a view, use the following code:
/// </para>
/// <code><![CDATA[
/// var container = new Container();
/// var docker = new CustomDocker();
/// var shellViewModel = new ShellViewModel(container, docker);
/// ]]></code>
/// </example>
[System.Diagnostics.CodeAnalysis.SuppressMessage("Maintainability", "CA1515:Consider making public types internal", Justification = "ViewModel must be public because the 'ViewModel' property in the View is public")]
public partial class ShellViewModel : ObservableObject
{
    private readonly IResolver resolver;

    /// <summary>
    /// Gets or sets the workspace view model.
    /// </summary>
    /// <value>
    /// The <see cref="WorkspaceViewModel"/> instance representing the workspace.
    /// </value>
    [ObservableProperty]
    private WorkspaceViewModel workspace;

    /// <summary>
    /// Initializes a new instance of the <see cref="ShellViewModel"/> class.
    /// </summary>
    /// <param name="resolver">The dependency injection container used to resolve services and view models.</param>
    /// <param name="docker">The docker instance used to manage the docking operations within the workspace.</param>
    public ShellViewModel(IResolver resolver, IDocker docker)
    {
        this.resolver = resolver;
        this.Workspace = this.CreateWorkspace(docker);
    }

    /// <summary>
    /// Creates and initializes the workspace view model.
    /// </summary>
    /// <param name="docker">The docker instance used to manage the docking operations within the workspace.</param>
    /// <returns>A new instance of <see cref="WorkspaceViewModel"/>.</returns>
    private WorkspaceViewModel CreateWorkspace(IDocker docker)
    {
        var dockViewFactory = this.resolver.Resolve<IDockViewFactory>();
        var layout = new GridFlowLayout(dockViewFactory);
        return this.resolver.Resolve<Func<IDocker, GridFlowLayout, WorkspaceViewModel>>()(docker, layout);
    }
}
