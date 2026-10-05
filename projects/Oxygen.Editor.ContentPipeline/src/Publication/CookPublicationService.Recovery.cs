// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Projects;

namespace Oxygen.Editor.ContentPipeline.Publication;

/// <summary>Selected-head admission and recovery share the same selection gate.</summary>
public sealed partial class CookPublicationService
{
    /// <summary>Acquires one coherent publication for cooking, status, catalogs or mounting.</summary>
    public async Task<CookPublicationReadLease> AcquireReadAsync(ProjectContext project, CancellationToken cancellationToken)
    {
        using var gate = await CookOutputLease.AcquireWriteAsync(project.ProjectRoot, cancellationToken).ConfigureAwait(false);
        await CookPublicationTransaction.RequireSettledSelectionAsync(project, files, gate, cancellationToken).ConfigureAwait(false);
        return await CookPublicationReadLease.OpenUnderGateAsync(project, files, gate, cancellationToken).ConfigureAwait(false);
    }

    internal async Task<CookPublicationReadLease> AcquireForOperationAsync(ContentCookOperation operation, CancellationToken cancellationToken)
    {
        coordinator.VerifyWriter(operation);
        using var gate = await CookOutputLease.AcquireWriteAsync(operation.Project.ProjectRoot, cancellationToken).ConfigureAwait(false);
        await CookPublicationTransaction.RecoverInterruptedMutationsAsync(operation.Project, files, projectManager, gate,
            operation.OperationId, cancellationToken).ConfigureAwait(false);
        var saved = await projectManager.LoadProjectInfoAsync(operation.Project.ProjectRoot).ConfigureAwait(false)
            ?? throw new InvalidDataException("The saved project is unavailable.");
        if (saved.Id != operation.Project.ProjectId
            || !string.Equals(CookPublicationDocument.ConfigurationIdentity(ProjectContext.FromProjectInfo(saved, []))
, CookPublicationDocument.ConfigurationIdentity(operation.Project), StringComparison.Ordinal))
        {
            throw new DroidNet.Storage.StorageWriteConflictException("The saved content configuration changed. Reopen the project before cooking or mounting it.");
        }

        return await CookPublicationReadLease.OpenUnderGateAsync(operation.Project, files, gate, cancellationToken).ConfigureAwait(false);
    }

    /// <summary>Restores interrupted authored-source/configuration changes before a host activates its project context.</summary>
    public async Task RecoverAsync(ProjectContext project, CancellationToken cancellationToken)
    {
        using var gate = await CookOutputLease.AcquireWriteAsync(project.ProjectRoot, cancellationToken).ConfigureAwait(false);
        await CookPublicationTransaction.RecoverInterruptedMutationsAsync(project, files, projectManager, gate,
            currentOperation: null, cancellationToken).ConfigureAwait(false);
    }
}
