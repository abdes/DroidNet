// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Projects;

namespace Oxygen.Editor.ContentPipeline.Publication;

/// <summary>Selected-head admission and recovery share the same selection gate.</summary>
public sealed partial class CookPublicationService
{
    /// <summary>Acquires one coherent publication for cooking, status, catalogs or mounting.</summary>
    /// <param name="project">The project whose publication is acquired.</param>
    /// <param name="cancellationToken">Cancels the acquisition.</param>
    /// <returns>A lease on the settled publication.</returns>
    public async Task<CookPublicationReadLease> AcquireReadAsync(ProjectContext project, CancellationToken cancellationToken)
    {
        using var gate = await CookOutputLease.AcquireWriteAsync(project.ProjectRoot, cancellationToken).ConfigureAwait(false);
        await CookPublicationTransaction.RequireSettledSelectionAsync(project, files, gate, cancellationToken).ConfigureAwait(false);
        return await CookPublicationReadLease.OpenUnderGateAsync(project, files, gate, cancellationToken).ConfigureAwait(false);
    }

    /// <summary>Restores interrupted authored-source/configuration changes before a host activates its project context.</summary>
    /// <param name="project">The project being activated.</param>
    /// <param name="cancellationToken">Cancels the recovery.</param>
    /// <returns>A task that completes when the project is settled.</returns>
    public async Task RecoverAsync(ProjectContext project, CancellationToken cancellationToken)
    {
        using var gate = await CookOutputLease.AcquireWriteAsync(project.ProjectRoot, cancellationToken).ConfigureAwait(false);
        await Relocation.AssetRelocationTransaction.RecoverAsync(project.ProjectRoot, files, this.Logger, cancellationToken).ConfigureAwait(false);
        await CookPublicationTransaction.RecoverInterruptedMutationsAsync(
            project,
            files,
            projectManager,
            gate,
            currentOperation: null,
            cancellationToken).ConfigureAwait(false);
    }

    /// <summary>Recovers interrupted mutations, then opens the publication a cook operation runs against.</summary>
    /// <param name="operation">The operation that owns the publication.</param>
    /// <param name="cancellationToken">Cancels the acquisition.</param>
    /// <returns>A lease on the settled publication.</returns>
    internal async Task<CookPublicationReadLease> AcquireForOperationAsync(ContentCookOperation operation, CancellationToken cancellationToken)
    {
        coordinator.VerifyWriter(operation);
        using var gate = await CookOutputLease.AcquireWriteAsync(operation.Project.ProjectRoot, cancellationToken).ConfigureAwait(false);
        await CookPublicationTransaction.RecoverInterruptedMutationsAsync(
            operation.Project,
            files,
            projectManager,
            gate,
            operation.OperationId,
            cancellationToken).ConfigureAwait(false);
        var saved = await projectManager.LoadProjectInfoAsync(operation.Project.ProjectRoot).ConfigureAwait(false)
            ?? throw new InvalidDataException("The saved project is unavailable.");
        var savedIdentity = CookPublicationDocument.ConfigurationIdentity(ProjectContext.FromProjectInfo(saved, []));
        if (saved.Id != operation.Project.ProjectId
            || !string.Equals(savedIdentity, CookPublicationDocument.ConfigurationIdentity(operation.Project), StringComparison.Ordinal))
        {
            throw new DroidNet.Storage.StorageWriteConflictException("The saved content configuration changed. Reopen the project before cooking or mounting it.");
        }

        return await CookPublicationReadLease.OpenUnderGateAsync(operation.Project, files, gate, cancellationToken).ConfigureAwait(false);
    }
}
