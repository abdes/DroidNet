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
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.SceneEditor.Settings;

/// <summary>
///     The scene editor's preferences stored per project, in a project session's scope: the preview settings and the
///     viewport layouts. They end with the session; the gizmo preferences, kept across projects, are the editor's.
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
