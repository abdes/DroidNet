// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Composition;
using DroidNet.Routing;
using Oxygen.Editor.Routing;
using Oxygen.Editor.Shell;
using Oxygen.Editor.Shell.Contributions;
using Oxygen.Editor.Shell.Layout;

namespace Oxygen.Editor;

/// <summary>
///     The modules of each kind of window of a project session, in registration and activation order: the one place that
///     shows what a window's scope contains.
/// </summary>
/// <remarks>
///     Every window's scope is created under its session's (<see cref="Sessions.ISessionWindows.OpenWindow" />), with
///     the window's id. The Shell provides the window modules; the features provide their panels, commands and the views
///     of their document editors.
/// </remarks>
public static class EditorWindowModules
{
    /// <summary>
    ///     Gets the modules of a workspace window's scope: its router over its routes, its docking, the Shell's window
    ///     services over the features' panels and commands, the features and the views of the documents it shows.
    /// </summary>
    /// <param name="routes">The window's routes.</param>
    /// <param name="context">The window's local router context.</param>
    /// <param name="features">The features whose panels and commands the window shows.</param>
    /// <param name="documentViews">The features' views of their document editors.</param>
    /// <returns>The modules.</returns>
    /// <remarks>The Shell's document area follows the window's tabs before the features activate.</remarks>
    public static IReadOnlyList<IScopeModule<IWindowLayer>> Workspace(
        IRoutes routes,
        LocalRouterContext context,
        IReadOnlyList<IFeatureModule> features,
        IReadOnlyList<IDocumentViewModule> documentViews)
    {
        ArgumentNullException.ThrowIfNull(features);
        ArgumentNullException.ThrowIfNull(documentViews);
        return
        [
            new RouterScopeModule(routes, context),
            new DockingWindowModule(),
            new ShellWindowModule(features),
            .. features,
            .. documentViews,
        ];
    }

    /// <summary>
    ///     Gets the modules of a document window's scope, the window a document's tab is torn out into: the Shell's
    ///     document window services and the views of the documents it shows.
    /// </summary>
    /// <param name="documentViews">The features' views of their document editors.</param>
    /// <returns>The modules.</returns>
    public static IReadOnlyList<IScopeModule<IWindowLayer>> Document(IReadOnlyList<IDocumentViewModule> documentViews)
    {
        ArgumentNullException.ThrowIfNull(documentViews);
        return [new ShellDocumentWindowModule(), .. documentViews];
    }
}
