// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.ContentPipeline.Import;
using Oxygen.Editor.ContentPipeline.Inspection;
using Oxygen.Editor.ContentPipeline.Publication;
using Oxygen.Editor.Projects;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.ContentPipeline;

/// <summary>Reads the declarations of scripts that come from mounted cooked libraries.</summary>
public sealed partial class ContentPipelineService : IScriptDeclarationProvider
{
    /// <summary>The native asset type of a script.</summary>
    private const byte ScriptAssetType = 4;

    /// <inheritdoc />
    async Task<CookedScriptDeclarations?> IScriptDeclarationProvider.ReadAsync(ProjectContext project, AssetPath scriptUri, CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(project);
        ArgumentNullException.ThrowIfNull(scriptUri);
        if (!ReferenceEquals(this.projectContextService.ActiveProject, project) || this.engineContentPipelineApi is not ICookedScriptInspector inspector)
        {
            return null;
        }

        using var cancellation = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
        using var projectChanges = this.projectContextService.ProjectChanged.Subscribe(current =>
        {
            if (!ReferenceEquals(current, project))
            {
                cancellation.Cancel();
            }
        });
        var token = cancellation.Token;
        using var publicationRead = await this.publication.AcquireReadAsync(project, token).ConfigureAwait(false);
        using var libraries = await CookedLibraryReadSet.AcquireAsync(project, this.engineContent, token, uri => ImportedOutputs.ResolveOutput(project, uri, ContentCookInputRole.Dependency), [.. publicationRead.ProductState.ProducedAssets]).ConfigureAwait(false);
        if (libraries.FindPreferredAsset(scriptUri) is not { } library)
        {
            return null;
        }

        try
        {
            await libraries.ValidateNativeAsync(this.engineContentPipelineApi, token).ConfigureAwait(false);
            var native = library.Asset.Cooked;
            if (native is null || native.AssetType != ScriptAssetType || native.VirtualPath is not { } virtualPath)
            {
                return null;
            }

            var operationRoot = Path.Combine(project.ProjectRoot.FullPath, ".build", "inspection");
            var report = await inspector.InspectScriptsAsync(operationRoot, library.CookedRoot, virtualPath.VirtualPath, token).ConfigureAwait(false);
            token.ThrowIfCancellationRequested();
            var declarations = report.SingleOrDefault(scriptUri);
            return declarations is not null && !string.Equals(declarations.NativeScriptKey.ToString("D"), native.AssetKey.ToString(), StringComparison.OrdinalIgnoreCase)
                ? throw new InvalidDataException("Native script declarations do not match the selected library's asset identity.")
                : declarations;
        }
        catch (ContentPipelineTerminationException failure)
        {
            await failure.DrainCompletion.ConfigureAwait(false);
            throw;
        }
    }
}
