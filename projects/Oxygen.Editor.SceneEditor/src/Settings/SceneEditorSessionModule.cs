// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Composition;
using DryIoc;
using Microsoft.Extensions.Logging;
using Oxygen.Editor.Data;
using Oxygen.Editor.Data.Services;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Runtime.Engine;
using Oxygen.Editor.Scenes.Views;
using Oxygen.Editor.Shell.Contributions;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.SceneEditor.Settings;

/// <summary>
///     The scene editor in a project session's scope: the editor of scene documents and the registry of the editors that
///     frame and move cameras, which every window of the session reaches, and the preferences stored per project, the
///     preview settings and the viewport layouts. They end with the session; the gizmo preferences, kept across
///     projects, are the editor's.
/// </summary>
public sealed class SceneEditorSessionModule : IScopeModule<ISessionLayer>
{
    /// <inheritdoc />
    public IReadOnlyCollection<Type> Shadows => [];

    /// <inheritdoc />
    public IReadOnlyList<Type> Activation => [];

    /// <inheritdoc />
    public void Register(IRegistrator scope)
    {
        ArgumentNullException.ThrowIfNull(scope);

        // The scene editors belong to the session; each window that shows one lends it the window's services.
        scope.Register<IDocumentEditorProvider, SceneEditorProvider>(Reuse.Singleton);
        scope.Register<SceneEditorViews>(Reuse.Singleton);
        scope.RegisterMapping<ISceneViews, SceneEditorViews>();

        // A dedicated manager owns the session's own database context; preview I/O is serialized by its service.
        scope.Register<IEditorSettingsManager, EditorSettingsManager>(Reuse.Singleton, serviceKey: nameof(PreviewSettingsService));
        scope.RegisterDelegate(
            resolver => new PreviewSettingsService(
                resolver.Resolve<IEngineService>(),
                resolver.Resolve<IEditorSettingsManager>(serviceKey: nameof(PreviewSettingsService)),
                resolver.Resolve<IProjectSession>(),
                resolver.Resolve<IOperationResultPublisher>(),
                resolver.Resolve<IStatusReducer>(),
                resolver.Resolve<ILoggerFactory>()),
            Reuse.Singleton);

        // Scene viewport layout and pane cameras: user-local workspace state, serialized writes.
        scope.RegisterDelegate(
            resolver => new ViewportStateService(
                resolver.Resolve<IEditorSettingsManager>(),
                resolver.Resolve<IOperationResultPublisher>(),
                resolver.Resolve<IStatusReducer>(),
                resolver.Resolve<ILoggerFactory>()),
            Reuse.Singleton);
    }
}
