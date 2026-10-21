// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DryIoc;
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

        // Per window on purpose: it observes this window's hidden nodes and ends with the window, so a second window
        // cannot inherit a stale notice.
        scope.RegisterDelegate(
            resolver => new EditingViewMaskService(
                resolver.Resolve<SceneNodeInteraction>(),
                resolver.Resolve<IOperationResultPublisher>(),
                resolver.Resolve<IStatusReducer>()).Start(),
            Reuse.Singleton);
    }
}
