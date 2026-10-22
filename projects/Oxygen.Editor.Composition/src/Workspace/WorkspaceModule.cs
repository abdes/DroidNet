// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Composition;
using DryIoc;
using Oxygen.Editor.ContentExplorer;
using Oxygen.Editor.CookedInspector;
using Oxygen.Editor.Cooking;
using Oxygen.Editor.MaterialEditor;
using Oxygen.Editor.Output;
using Oxygen.Editor.Problems;
using Oxygen.Editor.SceneEditor;
using Oxygen.Editor.SceneExplorer;
using Oxygen.Editor.SceneInspector;
using Oxygen.Editor.Shell.Contributions;

namespace Oxygen.Editor.Workspace;

/// <summary>
///     The workspace window in the editor scope: its view model and view, the features each workspace window shows, and
///     the views of the features' document editors, which every window that shows documents has. Each window registers
///     the features' services in its own scope.
/// </summary>
internal sealed class WorkspaceModule : IScopeModule<IApplicationLayer>
{
    /// <inheritdoc />
    public IReadOnlyCollection<Type> Shadows => [];

    /// <inheritdoc />
    public IReadOnlyList<Type> Activation => [];

    /// <inheritdoc />
    public void Register(IRegistrator scope)
    {
        ArgumentNullException.ThrowIfNull(scope);
        scope.Register<WorkspaceViewModel>(Reuse.Transient);
        scope.Register<WorkspaceView>(Reuse.Transient);

        scope.Register<IFeatureModule, ContentExplorerModule>(Reuse.Singleton);
        scope.Register<IFeatureModule, CookedInspectorModule>(Reuse.Singleton);
        scope.Register<IFeatureModule, CookingModule>(Reuse.Singleton);
        scope.Register<IFeatureModule, OutputModule>(Reuse.Singleton);
        scope.Register<IFeatureModule, ProblemsModule>(Reuse.Singleton);
        scope.Register<IFeatureModule, SceneEditorModule>(Reuse.Singleton);
        scope.Register<IFeatureModule, SceneExplorerModule>(Reuse.Singleton);
        scope.Register<IFeatureModule, SceneInspectorModule>(Reuse.Singleton);

        // The views of the editors whose documents move between the session's windows.
        scope.Register<IDocumentViewModule, MaterialEditorModule>(Reuse.Singleton);
    }
}
