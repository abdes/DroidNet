// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.Mvvm.ComponentModel;
using DroidNet.Routing;
using DryIoc;
using Microsoft.UI.Xaml;
using Oxygen.Editor.Sessions;
using Oxygen.Editor.Shell.Commands;
using Oxygen.Editor.Shell.Contributions;
using Oxygen.Editor.Shell.Documents;

namespace Oxygen.Editor.DocumentWindows;

/// <summary>
///     The page of a document window, the window a document's tab is torn out into: the window's document area, and the
///     commands its keys run.
/// </summary>
/// <remarks>
///     The window has its own scope under the open project session's, as the workspace window does, and carries the
///     session in its metadata: it is one of the session's windows, and closes with it. Its documents and their editors
///     stay the session's; the window shows their tabs. Closing the window disposes its scope
///     (<see cref="Shell.Windows.WindowScopeClose" />).
/// </remarks>
public sealed partial class DocumentWindowViewModel : ObservableObject, IRoutingAware
{
    private readonly ISessionWindows sessions;
    private readonly IReadOnlyList<IDocumentViewModule> documentViews;

    /// <summary>Initializes a new instance of the <see cref="DocumentWindowViewModel" /> class.</summary>
    /// <param name="sessions">The project session's windows, which creates the window's scope and owns it.</param>
    /// <param name="documentViews">The views of the features' document editors.</param>
    public DocumentWindowViewModel(ISessionWindows sessions, IEnumerable<IDocumentViewModule> documentViews)
    {
        ArgumentNullException.ThrowIfNull(documentViews);
        this.sessions = sessions;
        this.documentViews = [.. documentViews];
    }

    /// <summary>Gets the view model of the window's document area, once the window's scope is open.</summary>
    [ObservableProperty]
    public partial DocumentAreaViewModel? Area { get; private set; }

    /// <summary>Gets the window's commands, which its keys run, once the window's scope is open.</summary>
    public ICommandService? Commands { get; private set; }

    /// <inheritdoc />
    public Task OnNavigatedToAsync(IActiveRoute route, INavigationContext navigationContext)
    {
        ArgumentNullException.ThrowIfNull(navigationContext);
        if (this.Area is not null)
        {
            return Task.CompletedTask;
        }

        if (navigationContext.NavigationTarget is not Window window)
        {
            throw new InvalidOperationException("A document window's page shows only as the content of a window.");
        }

        var resolver = this.sessions.OpenWindow(window.AppWindow.Id, EditorWindowModules.Document(this.documentViews)).Resolver;
        this.Commands = resolver.Resolve<ICommandService>();
        this.Area = resolver.Resolve<DocumentAreaViewModel>();
        return Task.CompletedTask;
    }
}
