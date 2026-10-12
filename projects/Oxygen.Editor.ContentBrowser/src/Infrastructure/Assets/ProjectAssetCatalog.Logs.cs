// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.Extensions.Logging;
using Oxygen.Editor.ContentPipeline.Publication;

namespace Oxygen.Editor.ContentBrowser.Infrastructure.Assets;

/// <inheritdoc cref="ProjectAssetCatalog"/>
public sealed partial class ProjectAssetCatalog
{
    [LoggerMessage(
        Level = LogLevel.Warning,
        Message = "Cooked {Owner} root '{RootName}' at '{RootPath}' changed after publication {PublicationId} of project '{ProjectRoot}': "
            + "recorded source {RecordedSourceKey} index {RecordedIndexSha256}, found source {ActualSourceKey} index {ActualIndexSha256}. "
            + "Republishing the project from its current libraries.")]
    private partial void LogCookedRootChangedCore(
        CookPublicationRootOwner? owner,
        string? rootName,
        string? rootPath,
        Guid? publicationId,
        string projectRoot,
        Guid? recordedSourceKey,
        string? recordedIndexSha256,
        Guid? actualSourceKey,
        string? actualIndexSha256);

    [LoggerMessage(Level = LogLevel.Information, Message = "Republished cooked content of project '{ProjectRoot}' as publication {PublicationId}; the asset catalog now uses it.")]
    private partial void LogRepublished(Guid? publicationId, string projectRoot);

    [LoggerMessage(Level = LogLevel.Error, Message = "Could not republish cooked content of project '{ProjectRoot}'; cooked assets stay unavailable while authored assets remain browsable.")]
    private partial void LogRepublishFailed(Exception exception, string projectRoot);

    private void LogCookedRootChanged(CookedLibraryChangedException changed, Guid? publicationId, string projectRoot)
        => this.LogCookedRootChangedCore(
            changed.Root?.Owner,
            changed.Root?.Name,
            changed.RootPath,
            publicationId,
            projectRoot,
            changed.Root?.SourceKey,
            changed.Root?.IndexSha256,
            changed.ActualSourceKey,
            changed.ActualIndexSha256);
}
