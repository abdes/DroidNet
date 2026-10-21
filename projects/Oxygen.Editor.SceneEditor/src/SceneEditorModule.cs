// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DryIoc;
using Microsoft.Extensions.Logging;
using Oxygen.Editor.Data;
using Oxygen.Editor.Data.Services;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Runtime.Engine;
using Oxygen.Editor.SceneEditor.Settings;
using Oxygen.Editor.Scenes.Interaction;
using Oxygen.Editor.Scenes.Views;
using Oxygen.Editor.Shell.Commands;
using Oxygen.Editor.Shell.Contributions;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.SceneEditor;

/// <summary>
/// The scene editor: the editor of scene documents, with the scene views and commands it implements and the window
/// services that follow its scenes.
/// </summary>
public sealed class SceneEditorModule : IFeatureModule
{
    /// <inheritdoc />
    public IReadOnlyList<PanelDescriptor> Panels { get; } = [];

    /// <inheritdoc />
    public IReadOnlyList<CommandDescriptor> Commands => SceneEditorCommands.All;

    /// <inheritdoc />
    public IReadOnlyCollection<Type> Shadows => [];

    /// <inheritdoc />
    /// <remarks>
    /// The window's scenes are followed from the start: the last scene reopens when one becomes unavailable, and the
    /// hidden nodes' notice and the preview settings end with the window.
    /// </remarks>
    public IReadOnlyList<Type> Activation { get; } = [typeof(SceneDocumentLifetime), typeof(EditingViewMaskService), typeof(PreviewSettingsLifetime)];

    /// <inheritdoc />
    public void Register(IRegistrator scope)
    {
        ArgumentNullException.ThrowIfNull(scope);
        scope.Register<IDocumentEditorProvider, SceneEditorProvider>(Reuse.Singleton);
        scope.Register<SceneEditorView>(Reuse.Transient);
        scope.Register<SceneEditorViews>(Reuse.Singleton);
        scope.RegisterMapping<ISceneViews, SceneEditorViews>();
        scope.Register<SceneDocumentLifetime>(Reuse.Singleton);
        scope.RegisterMapping<ISceneAvailability, SceneDocumentLifetime>();
        scope.Register<PreviewSettingsLifetime>(Reuse.Singleton);
        RegisterPreferences(scope);

        // Per window on purpose: it observes this window's hidden nodes and ends with the window, so a second window
        // cannot inherit a stale notice.
        scope.RegisterDelegate(
            resolver => new EditingViewMaskService(
                resolver.Resolve<SceneNodeInteraction>(),
                resolver.Resolve<IOperationResultPublisher>(),
                resolver.Resolve<IStatusReducer>()).Start(),
            Reuse.Singleton);
    }

    // The preferences stored per project: preview settings and viewport layouts. They are the window's until the
    // project session owns them; the gizmo preferences, kept across projects, are the editor's.
    private static void RegisterPreferences(IRegistrator scope)
    {
        // A dedicated manager owns a separate DbContext; preview I/O is serialized by its service.
        scope.Register<IEditorSettingsManager, EditorSettingsManager>(Reuse.Singleton, serviceKey: nameof(PreviewSettingsService));
        scope.RegisterDelegate(
            resolver => new PreviewSettingsService(
                resolver.Resolve<IEngineService>(),
                resolver.Resolve<IEditorSettingsManager>(serviceKey: nameof(PreviewSettingsService)),
                resolver.Resolve<IProjectContextService>(),
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
