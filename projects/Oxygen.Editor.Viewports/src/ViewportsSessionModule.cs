// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Composition;
using DryIoc;
using Microsoft.Extensions.Logging;
using Oxygen.Editor.Data.Services;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Scenes.Views;
using Oxygen.Editor.Viewports.Settings;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.Viewports;

/// <summary>
/// The viewports in a project session's scope: <see cref="ViewportServices"/>, through which the scene editor and the
/// model editor create their viewports and outline their selection, and <see cref="SceneViews"/>, through which other
/// panels reach those viewports, and <see cref="ViewportStateService"/>, which keeps each scene's and each model's
/// viewport layout and cameras per user.
/// </summary>
public sealed class ViewportsSessionModule : IScopeModule<ISessionLayer>
{
    /// <inheritdoc />
    public IReadOnlyCollection<Type> Shadows => [];

    /// <inheritdoc />
    public IReadOnlyList<Type> Activation => [];

    /// <inheritdoc />
    public void Register(IRegistrator scope)
    {
        ArgumentNullException.ThrowIfNull(scope);
        scope.Register<ViewportServices>(Reuse.Singleton);
        scope.Register<SceneViews>(Reuse.Singleton);
        scope.RegisterMapping<ISceneViews, SceneViews>();

        // Each scene's and each model's viewport layout and pane cameras: user-local workspace state, serialized writes.
        scope.RegisterDelegate(
            resolver => new ViewportStateService(
                resolver.Resolve<IProjectSession>(),
                resolver.Resolve<IEditorSettingsManager>(),
                resolver.Resolve<IOperationResultPublisher>(),
                resolver.Resolve<IStatusReducer>(),
                resolver.Resolve<ILoggerFactory>()),
            Reuse.Singleton);
    }
}
