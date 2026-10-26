// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Composition;
using DroidNet.Storage;
using DryIoc;
using Microsoft.Extensions.Logging;
using Oxygen.Editor.Assets;
using Oxygen.Editor.ContentPipeline;
using Oxygen.Editor.Data;
using Oxygen.Editor.Documents;
using Oxygen.Editor.Input;
using Oxygen.Editor.LivePreview;
using Oxygen.Editor.LivePreview.Content;
using Oxygen.Editor.MaterialEditor;
using Oxygen.Editor.ModelEditor;
using Oxygen.Editor.Materials;
using Oxygen.Editor.Materials.Documents;
using Oxygen.Editor.Problems;
using Oxygen.Editor.Projects;
using Oxygen.Editor.SceneEditor.Settings;
using Oxygen.Editor.Scenes;
using Oxygen.Editor.Scenes.Documents;
using Oxygen.Editor.Scripting;
using Oxygen.Editor.Shell;
using Oxygen.Editor.Shell.Desktop;

namespace Oxygen.Editor.Sessions;

/// <summary>
///     The project session's own services in the composition root: the session's open documents, of the kinds the
///     editor edits, the content their scenes need for the preview, the project folder that desktop tools open, and the
///     session's own contexts over the editor's state database.
/// </summary>
/// <remarks>
///     <para>
///     The session's scope composes this module with the session modules of the subsystems (<see cref="Compose" />).
///     The session registers its loaded project and itself as instances, after its modules. It registers no
///     <see cref="Projects.ProjectContext" />: a service reads it from the session, which a mount change updates.
///     </para>
///     <para>
///     The services of the session that take a <see cref="PersistentState" /> share the one context the session rents
///     from the editor's pool and returns when it closes, never one the editor scope would keep until the editor exits.
///     </para>
/// </remarks>
public sealed class ProjectSessionModule : IScopeModule<ISessionLayer>
{
    // The preview content demand follows the open scene documents.
    private static readonly Type[] Activated = [typeof(PreviewContentDemand)];

    /// <inheritdoc />
    public IReadOnlyCollection<Type> Shadows => [typeof(PersistentState)];

    /// <inheritdoc />
    public IReadOnlyList<Type> Activation => Activated;

    /// <summary>Gets the modules of a project session's scope.</summary>
    /// <returns>The modules, in registration and activation order.</returns>
    /// <remarks>
    ///     The automatic cook listens to the catalog, then the engine preview and the Play session are created, then the
    ///     preview content demand. The problem sources are listed in registration order: scripts, input, then Play.
    /// </remarks>
    public static IReadOnlyList<IScopeModule<ISessionLayer>> Compose()
        =>
        [
            new ShellSessionModule(),
            new ContentPipelineSessionModule(),
            new AssetsSessionModule(),
            new ScriptingSessionModule(),
            new InputSessionModule(),
            new LivePreviewSessionModule(),
            new ProblemsSessionModule(),
            new MaterialsSessionModule(),
            new ScenesSessionModule(),
            new SceneEditorSessionModule(),
            new MaterialEditorSessionModule(),
            new ModelEditorSessionModule(),
            new ProjectSessionModule(),
        ];

    /// <inheritdoc />
    public void Register(IRegistrator scope)
    {
        ArgumentNullException.ThrowIfNull(scope);
        scope.Register<IProjectFolder, ProjectFolder>(Reuse.Singleton);

        // Each file opens once in the session, as a document of the kind registered for its suffix, whichever window
        // shows it.
        scope.RegisterDelegate(
            resolver => new OpenDocuments(
                [resolver.Resolve<SceneDocumentKind>(), resolver.Resolve<MaterialDocumentKind>()],
                resolver.Resolve<IAtomicFileStore>(),
                resolver.Resolve<ILoggerFactory>()),
            Reuse.Singleton);
        RegisterPreviewContentDemand(scope);
        scope.RegisterDelegate<PersistentState>(EditorDataModule.CreatePersistentState, Reuse.Singleton);
    }

    // The preview content demand follows the open scene documents.
    private static void RegisterPreviewContentDemand(IRegistrator scope)
        => scope.RegisterDelegate(
            resolver => new PreviewContentDemand(ContentDemands.Start(
                resolver.Resolve<OpenDocuments>(),
                resolver.Resolve<IAssetFeed>(),
                resolver.Resolve<IContentPipelineService>(),
                resolver.Resolve<IProjectSession>(),
                resolver.Resolve<ILoggerFactory>())),
            Reuse.Singleton);
}
