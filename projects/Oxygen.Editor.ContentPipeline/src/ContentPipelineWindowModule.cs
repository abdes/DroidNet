// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Composition;
using DryIoc;
using Oxygen.Editor.ContentPipeline.Authoring;
using Oxygen.Editor.ContentPipeline.Cooking;
using Oxygen.Editor.ContentPipeline.Relocation;
using Oxygen.Editor.ContentPipeline.Status;
using Oxygen.Editor.Documents;

namespace Oxygen.Editor.ContentPipeline;

/// <summary>
///     The content pipeline in a workspace window's scope: the cook status of the window's assets, relocation and
///     creation of assets, the automatic cook and the unsaved inputs it waits for, and the project's files and
///     relocations as documents see them.
/// </summary>
/// <remarks>
///     The Content Explorer's scope reaches the same instances, created by the window's scope whichever scope asks
///     first. The project session owns these services once it exists.
/// </remarks>
public sealed class ContentPipelineWindowModule : IScopeModule<IWindowLayer>
{
    // The cook listens to the catalog's changes from the moment the workspace opens.
    private static readonly Type[] Activated = [typeof(IAutomaticCookService)];

    /// <inheritdoc />
    public IReadOnlyCollection<Type> Shadows => [];

    /// <inheritdoc />
    public IReadOnlyList<Type> Activation => Activated;

    /// <inheritdoc />
    public void Register(IRegistrator scope)
    {
        ArgumentNullException.ThrowIfNull(scope);
        scope.RegisterDelegate<IAssetCookStatusReader>(
            resolver => resolver.Resolve<IContentPipelineService>(), Reuse.Singleton);
        scope.Register<IAssetRelocationService, AssetRelocationService>(Reuse.Singleton);
        scope.Register<IAssetAuthoring, AssetAuthoring>(Reuse.Singleton);
        scope.Register<IAutomaticCookService, AutomaticCookService>(Reuse.Singleton);
        scope.Register<IUnsavedCookInputs, UnsavedCookInputs>(Reuse.Singleton);

        // The project's files and relocations, as documents see them.
        scope.Register<IDocumentFiles, ProjectDocumentFiles>(Reuse.Singleton);
        scope.Register<IDocumentRelocations, DocumentRelocationFeed>(Reuse.Singleton);
    }
}
