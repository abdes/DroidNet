// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Hosting.WinUI;
using Oxygen.Editor.ContentBrowser.Infrastructure.Assets;
using Oxygen.Editor.ContentPipeline;
using Oxygen.Editor.ContentPipeline.Publication;
using Oxygen.Editor.Projects;

namespace Oxygen.Editor.World.Services;

/// <summary>Applies confirmed mount configuration while retaining native and persisted ownership at its commit point.</summary>
/// <param name="coordinator">The shared project writer.</param>
/// <param name="projects">The active project context.</param>
/// <param name="publication">The journaled source, configuration and native publication owner.</param>
/// <param name="catalog">The accepted browser catalog.</param>
/// <param name="hosting">The owning workspace dispatcher.</param>
public sealed class ContentMountChangeService(IContentCookCoordinator coordinator, IProjectContextService projects, CookPublicationService publication, IProjectAssetCatalog catalog, HostingContext hosting)
{
    private readonly IContentCookCoordinator coordinator = coordinator;
    private readonly IProjectContextService projects = projects;
    private readonly CookPublicationService publication = publication;
    private readonly IProjectAssetCatalog catalog = catalog;

    /// <summary>Validates and commits one confirmed change, preserving old roots if it fails before saving.</summary>
    /// <param name="expected">The configuration accepted when editing began.</param>
    /// <param name="configuration">The candidate manifest.</param>
    /// <param name="beforeActivate">Updates workspace-owned registrations immediately before context publication.</param>
    /// <param name="cancellationToken">Cancels queued or uncommitted work.</param>
    /// <returns>Completion after native refresh, save and context publication settle.</returns>
    public Task ApplyAsync(ProjectContext expected, ProjectInfo configuration, Action<ProjectContext> beforeActivate, CancellationToken cancellationToken)
        => this.coordinator.RunProjectChangeAsync((operation, token) => hosting.Dispatcher.DispatchAsync(() => this.ApplyCoreAsync(expected, configuration, beforeActivate, operation, token)), cancellationToken);

    private static ProjectContext CreateCandidateContext(ProjectContext expected, ProjectInfo configuration)
    {
        if (configuration.Id != expected.ProjectId || configuration.Location is null
            || !string.Equals(Path.GetFullPath(configuration.Location), Path.GetFullPath(expected.ProjectRoot), StringComparison.OrdinalIgnoreCase))
        {
            throw new InvalidOperationException("A content mount change must stay within the active project.");
        }

        ValidateMountConfiguration(configuration);
        return ProjectContext.FromProjectInfo(configuration, expected.Scenes) with
        {
            OpenInitialScene = expected.OpenInitialScene,
            InitialSceneAssetUri = expected.InitialSceneAssetUri,
        };
    }

    private static ProjectInfo CreateProjectInfo(ProjectContext context) => new(context.ProjectId, context.Name, context.Category, context.ProjectRoot, context.Thumbnail)
    {
        AuthoringMounts = [.. context.AuthoringMounts],
        LocalFolderMounts = [.. context.LocalFolderMounts],
        CookedContentOrder = [.. context.CookedContentOrder],
        DefaultSceneId = context.DefaultSceneId,
    };

    private static void ValidateMountConfiguration(ProjectInfo candidate)
    {
        var names = candidate.AuthoringMounts.Select(static mount => mount.Name).Concat(candidate.LocalFolderMounts.Select(static mount => mount.Name)).ToArray();
        if (names.Any(static name => string.IsNullOrWhiteSpace(name) || name is "." or ".." || name.IndexOfAny(['/', '\\', ':']) >= 0)
            || names.ToHashSet(StringComparer.OrdinalIgnoreCase).Count != names.Length)
        {
            throw new InvalidDataException("Content mount names must be unique folder names.");
        }

        if (!candidate.AuthoringMounts.Any(static mount => mount.RelativePath.Trim().Replace('\\', '/').Trim('/') is not (".cooked" or ".imported" or ".build")))
        {
            throw new InvalidDataException("The project must keep at least one authoring content folder.");
        }

        foreach (var folder in candidate.LocalFolderMounts)
        {
            if (!Path.IsPathFullyQualified(folder.AbsolutePath) || !Directory.Exists(folder.AbsolutePath))
            {
                throw new DirectoryNotFoundException($"Local folder '{folder.Name}' is unavailable: {folder.AbsolutePath}.");
            }
        }

        _ = CookedContentOrdering.Resolve(candidate.LocalFolderMounts, candidate.CookedContentOrder);
    }

    private async Task ApplyCoreAsync(ProjectContext expected, ProjectInfo configuration, Action<ProjectContext> beforeActivate, ContentCookOperation operation, CancellationToken token)
    {
        this.VerifyExpectedContext(expected, operation);
        var next = CreateCandidateContext(expected, configuration);
        using var accepted = await this.publication.ChangeMountsAsync(operation, next, CreateProjectInfo(expected), configuration, token).ConfigureAwait(true);
        if (!ReferenceEquals(expected, this.projects.ActiveProject))
        {
            return;
        }

        // Publication has committed both saved intent and native admission.
        // Workspace notification is derived state and runs outside the head gate.
        beforeActivate(next);
        this.projects.Activate(next);
        await this.catalog.RefreshAsync(accepted, CancellationToken.None).ConfigureAwait(true);
    }

    private void VerifyExpectedContext(ProjectContext expected, ContentCookOperation operation)
    {
        if (!ReferenceEquals(expected, operation.Project) || !ReferenceEquals(expected, this.projects.ActiveProject))
        {
            throw new OperationCanceledException("The project configuration changed before this mount operation started.");
        }
    }
}
