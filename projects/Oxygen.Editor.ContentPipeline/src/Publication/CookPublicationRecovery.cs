// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Storage;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Oxygen.Editor.Projects;

namespace Oxygen.Editor.ContentPipeline.Publication;

/// <summary>
///     Restores a project's interrupted relocations and publication changes before the project opens, so its session
///     starts from a settled project.
/// </summary>
/// <remarks>
///     Recovery runs before any session exists, under the project's cook output lock, against the saved project. It can
///     restore the project's manifest, which is why the project is validated and loaded only after it.
/// </remarks>
/// <param name="files">The atomic file store.</param>
/// <param name="projectManager">Reads and restores the saved project configuration.</param>
/// <param name="loggerFactory">Optional factory of the logger that records what recovery restores.</param>
public sealed class CookPublicationRecovery(IAtomicFileStore files, IProjectManagerService projectManager, ILoggerFactory? loggerFactory = null)
{
    private readonly ILogger logger = loggerFactory?.CreateLogger<CookPublicationRecovery>() ?? NullLogger<CookPublicationRecovery>.Instance;

    /// <summary>Restores the interrupted changes of a project.</summary>
    /// <param name="project">The project about to open.</param>
    /// <param name="cancellationToken">Cancels the recovery.</param>
    /// <returns>A task that completes when the project is settled.</returns>
    public async Task RecoverAsync(ProjectContext project, CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(project);
        using var gate = await CookOutputLease.AcquireWriteAsync(project.ProjectRoot, cancellationToken).ConfigureAwait(false);
        await Relocation.AssetRelocationTransaction.RecoverAsync(project, files, this.logger, cancellationToken).ConfigureAwait(false);
        await CookPublicationTransaction.RecoverInterruptedMutationsAsync(
            project,
            files,
            projectManager,
            gate,
            currentOperation: null,
            cancellationToken).ConfigureAwait(false);
    }
}
