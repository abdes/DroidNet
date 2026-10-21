// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.ContentPipeline.Import;
using Oxygen.Editor.ContentPipeline.Inspection;
using Oxygen.Editor.ContentPipeline.Publication;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Scripting;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.ContentPipeline;

/// <summary>Reads the scripts of mounted cooked libraries, which have no source to analyze.</summary>
public sealed partial class ContentPipelineService : IScriptLibrary
{
    /// <summary>The native asset type of a script.</summary>
    private const byte ScriptAssetType = 4;

    /// <inheritdoc />
    async Task<Script?> IScriptLibrary.ReadAsync(ProjectContext project, AssetPath script, CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(project);
        ArgumentNullException.ThrowIfNull(script);
        if (!ReferenceEquals(this.session.Context, project) || this.engineContentPipelineApi is not ICookedScriptInspector inspector)
        {
            return null;
        }

        using var cancellation = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken, this.session.Lifetime);
        var token = cancellation.Token;
        using var publicationRead = await this.publication.AcquireReadAsync(project, token).ConfigureAwait(false);
        using var libraries = await CookedLibraryReadSet.AcquireAsync(project, this.engineContent, token, uri => ImportedOutputs.ResolveOutput(project, uri, ContentCookInputRole.Dependency), [.. publicationRead.ProductState.ProducedAssets]).ConfigureAwait(false);
        if (libraries.FindPreferredAsset(script) is not { } library)
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
            var cooked = report.SingleOrDefault(script);
            return cooked is not null && !string.Equals(cooked.AssetKey.ToString("D"), native.AssetKey.ToString(), StringComparison.OrdinalIgnoreCase)
                ? throw new InvalidDataException("Native script declarations do not match the selected library's asset identity.")
                : cooked?.Script;
        }
        catch (ContentPipelineTerminationException failure)
        {
            // The port reports a library it cannot read as an I/O failure, once the native reader has drained.
            await failure.DrainCompletion.ConfigureAwait(false);
            throw new IOException(failure.Message, failure);
        }
    }
}
