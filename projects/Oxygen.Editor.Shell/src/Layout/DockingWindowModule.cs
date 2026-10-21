// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Composition;
using DroidNet.Docking;
using DroidNet.Docking.Controls;
using DroidNet.Docking.Layouts;
using DroidNet.Docking.Workspace;
using DryIoc;
using Oxygen.Editor.Shell.Scopes;

namespace Oxygen.Editor.Shell.Layout;

/// <summary>
/// A workspace window's docking: its docker, the views of its docks and its layout. The dock panels are built in the
/// window's scope, so they see the window's view location.
/// </summary>
public sealed class DockingWindowModule : IScopeModule<IWindowLayer>
{
    /// <inheritdoc />
    public IReadOnlyCollection<Type> Shadows => [];

    /// <inheritdoc />
    public IReadOnlyList<Type> Activation => [];

    /// <inheritdoc />
    public void Register(IRegistrator scope)
    {
        ArgumentNullException.ThrowIfNull(scope);
        scope.Register<IDocker, Docker>(Reuse.Singleton);
        scope.Register<IDockViewFactory, DockViewFactory>(Reuse.Singleton);
        scope.Register<DockingWorkspaceLayout>(Reuse.Singleton);
        scope.Register<DockPanelViewModel>(Reuse.Transient);
        scope.Register<DockPanel>(Reuse.Transient);
    }
}
