// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Composition;
using DroidNet.Storage;
using DryIoc;
using Microsoft.Extensions.Logging;
using Oxygen.Editor.Assets;
using Oxygen.Editor.ContentPipeline;
using Oxygen.Editor.Documents;
using Oxygen.Editor.Input;
using Oxygen.Editor.LivePreview;
using Oxygen.Editor.LivePreview.Content;
using Oxygen.Editor.Materials.Documents;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Scenes;
using Oxygen.Editor.Scenes.Documents;
using Oxygen.Editor.Scripting;
using Oxygen.Editor.Shell;
using Oxygen.Editor.Shell.Contributions;

namespace Oxygen.Editor.Workspace;

/// <summary>
///     The workspace window's own services: the panels and commands its features contribute, the documents it opens,
///     of the kinds it edits, and the content those documents need for the preview.
/// </summary>
/// <remarks>
///     The window's scope composes this module with the window modules of the subsystems it uses and with its features
///     (<see cref="Compose" />), after its router and docking.
/// </remarks>
/// <param name="features">The features whose panels and commands the window shows.</param>
public sealed class WorkspaceWindowModule(IReadOnlyList<IFeatureModule> features) : IScopeModule<IWindowLayer>
{
    // The preview content demand follows the open scene documents.
    private static readonly Type[] Activated = [typeof(PreviewContentDemand)];

    /// <inheritdoc />
    public IReadOnlyCollection<Type> Shadows => [];

    /// <inheritdoc />
    public IReadOnlyList<Type> Activation => Activated;

    /// <summary>
    ///     Gets the modules of a workspace window's scope after its router and docking: the window modules of the
    ///     subsystems, this module, the Shell's and the features'.
    /// </summary>
    /// <param name="features">The features whose panels and commands the window shows.</param>
    /// <returns>The modules, in registration and activation order.</returns>
    /// <remarks>
    ///     The engine preview and the Play session are created first, then the preview content demand; the Shell's
    ///     document service then follows the window's tabs, and the automatic cook listens to the catalog, before the
    ///     features activate. The asset problems are listed in registration order: scripts, input, then Play.
    /// </remarks>
    public static IReadOnlyList<IScopeModule<IWindowLayer>> Compose(IReadOnlyList<IFeatureModule> features)
    {
        ArgumentNullException.ThrowIfNull(features);
        return
        [
            new AssetsWindowModule(),
            new ScenesWindowModule(),
            new ScriptingWindowModule(),
            new InputWindowModule(),
            new LivePreviewWindowModule(),
            new WorkspaceWindowModule(features),
            new ShellWindowModule(),
            new ContentPipelineWindowModule(),
            .. features,
        ];
    }

    /// <inheritdoc />
    public void Register(IRegistrator scope)
    {
        ArgumentNullException.ThrowIfNull(scope);
        RegisterContributions(scope, features);
        RegisterDocuments(scope);
        RegisterPreviewContentDemand(scope);
    }

    // The panels the features contribute, which the registry checks once.
    private static void RegisterContributions(IRegistrator scope, IReadOnlyList<IFeatureModule> modules)
        => scope.RegisterDelegate(
            _ => new ContributionRegistry(
                [.. modules.SelectMany(static module => module.Panels)],
                [.. modules.SelectMany(static module => module.Commands)]),
            Reuse.Singleton);

    // Each file opens once, as a document of the kind registered for its suffix, shown by that kind's editor.
    private static void RegisterDocuments(IRegistrator scope)
        => scope.RegisterDelegate(
            resolver => new OpenDocuments(
                [resolver.Resolve<SceneDocumentKind>(), resolver.Resolve<MaterialDocumentKind>()],
                resolver.Resolve<IAtomicFileStore>(),
                resolver.Resolve<ILoggerFactory>(IfUnresolved.ReturnDefault)),
            Reuse.Singleton);

    // The preview content demand follows the open scene documents.
    private static void RegisterPreviewContentDemand(IRegistrator scope)
        => scope.RegisterDelegate(
            resolver => new PreviewContentDemand(ContentDemands.Start(
                resolver.Resolve<OpenDocuments>(),
                resolver.Resolve<IAssetFeed>(),
                resolver.Resolve<IContentPipelineService>(),
                resolver.Resolve<IProjectContextService>(),
                resolver.Resolve<ILoggerFactory>(IfUnresolved.ReturnDefault))),
            Reuse.Singleton);
}
