// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using DroidNet.Routing;
using Oxygen.Editor.Data.Services;
using Oxygen.Editor.ProjectExplorer.Activation;
using Oxygen.Editor.ProjectExplorer.Projects;
using Oxygen.Editor.Projects;
using Oxygen.Managed.Core;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.Services;

/// <summary>
///     Host-owned coordinator for project open/create and workspace activation.
/// </summary>
/// <remarks>
///     Staging, publication recovery and the close of every window's documents happen before the project's session
///     opens. When the user cancels that close, the open session and its clipboard are kept. Once committed, the
///     previous session closes, the new one opens and the workspace window navigates into it; a navigation that reports
///     a failure closes the half-open session again.
/// </remarks>
[SuppressMessage("Performance", "CA1812:Avoid uninstantiated internal classes", Justification = "Instantiated through the singleton registration of the editor module.")]
internal sealed partial class ProjectActivationCoordinator(
    IRouter router,
    IProjectValidationService validation,
    IProjectCreationService creation,
    IProjectManagerService projectManager,
    Oxygen.Editor.ContentPipeline.Publication.CookPublicationRecovery recovery,
    IProjectSessionHost sessions,
    IRecentProjectAdapter recentProjects,
    ITemplateUsageService templateUsage,
    IProjectExplorerService projectExplorer,
    Oxygen.Editor.Shell.Documents.IDocumentTabs documents,
    IOperationResultPublisher operationResults,
    IStatusReducer statusReducer,
    IExceptionDiagnosticAdapter exceptionAdapter,
    System.IO.Abstractions.IFileSystem fileSystem) : IProjectActivationCoordinator, IDisposable
{
    private const string ProjectOpenOperation = "Project.Open";
    private const string ProjectCreateOperation = "Project.Create";
    private const string InvalidRequestCode = DiagnosticCodes.ProjectPrefix + "REQUEST_INVALID";
    private const string ValidationFailedCode = DiagnosticCodes.ProjectPrefix + "VALIDATION_FAILED";
    private const string CreationFailedCode = DiagnosticCodes.ProjectPrefix + "CREATE_FAILED";
    private const string LoadFailedCode = DiagnosticCodes.ProjectPrefix + "LOAD_FAILED";
    private const string NavigationFailedCode = DiagnosticCodes.WorkspacePrefix + "ACTIVATION_FAILED";
    private const string RecentUsageWarningCode = DiagnosticCodes.ProjectPrefix + "RECENT_USAGE_UPDATE_FAILED";
    private const string TemplateUsageWarningCode = DiagnosticCodes.ProjectPrefix + "TEMPLATE_USAGE_UPDATE_FAILED";
    private const string SettingsWarningCode = DiagnosticCodes.ProjectPrefix + "SETTINGS_UPDATE_FAILED";
    private readonly SemaphoreSlim gate = new(1, 1);
    private ProjectLoadSnapshot? stagedProject;

    /// <inheritdoc/>
    [SuppressMessage(
        "Design",
        "CA1031:Do not catch general exception types",
        Justification = "Activation must publish one operation result instead of leaking workflow exceptions to the UI.")]
    public async Task<OperationResult> ActivateAsync(
        ProjectActivationRequest request,
        CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(request);

        await this.gate.WaitAsync(cancellationToken).ConfigureAwait(true);
        try
        {
            this.stagedProject = null;
            return await this.ActivateCoreAsync(request, cancellationToken).ConfigureAwait(true);
        }
        finally
        {
            _ = this.gate.Release();
        }
    }

    /// <inheritdoc/>
    public void Dispose() => this.gate.Dispose();

    [SuppressMessage(
        "Maintainability",
        "MA0051:Method is too long",
        Justification = "The coordinator keeps one top-level activation workflow readable until ED-M01.5 extracts UI result presentation.")]
    [SuppressMessage(
        "Design",
        "CA1031:Do not catch general exception types",
        Justification = "Activation must publish one operation result instead of leaking workflow exceptions to the UI.")]
    private async Task<OperationResult> ActivateCoreAsync(
        ProjectActivationRequest request,
        CancellationToken cancellationToken)
    {
        var operationId = request.CorrelationId == Guid.Empty ? Guid.NewGuid() : request.CorrelationId;
        var startedAt = request.RequestedAt;
        var operationKind = request.Mode == ProjectActivationMode.CreateFromTemplate
            ? ProjectCreateOperation
            : ProjectOpenOperation;
        var diagnostics = new List<DiagnosticRecord>();
        var primaryGoalCompleted = false;
        var wasCancelled = false;
        ProjectContext? context = null;
        IProjectSession? opened = null;
        var affectedScope = AffectedScope.Empty;
        var title = request.Mode == ProjectActivationMode.CreateFromTemplate
            ? "Project creation failed"
            : "Project open failed";
        var message = "The workspace was not activated.";

        try
        {
            context = request.Mode switch
            {
                ProjectActivationMode.OpenExisting => await this.OpenExistingProjectAsync(
                        request,
                        operationId,
                        diagnostics,
                        cancellationToken)
                    .ConfigureAwait(true),

                ProjectActivationMode.CreateFromTemplate => await this.CreateProjectAsync(
                        request,
                        operationId,
                        diagnostics,
                        cancellationToken)
                    .ConfigureAwait(true),

                _ => throw new InvalidOperationException($"Unsupported project activation mode '{request.Mode}'."),
            };

            if (context is null)
            {
                ApplyFailureMessageFromDiagnostics();
                return PublishResult(completed: false);
            }

            affectedScope = new AffectedScope
            {
                ProjectId = context.ProjectId,
                ProjectName = context.Name,
                ProjectPath = context.ProjectRoot.FullPath,
            };

            var activationContext = new ProjectActivationContext
            {
                ProjectContext = context,
                RecentEntryId = request.RecentEntryId,
                RestorationRequest = new WorkspaceRestorationRequest
                {
                    ProjectId = context.ProjectId,
                    ProjectRoot = context.ProjectRoot,
                },
            };

            if (this.stagedProject is null || !projectManager.IsProjectLoadCurrent(this.stagedProject))
            {
                return PublishSuperseded();
            }

            using var replacement = await documents.PrepareCloseAllWindowsAsync().ConfigureAwait(true);
            cancellationToken.ThrowIfCancellationRequested();
            if (replacement is null || this.stagedProject is null
                || !await replacement.CommitAsync().ConfigureAwait(true))
            {
                wasCancelled = true;
                title = "Project activation cancelled";
                message = "The current workspace and its clipboard were kept open.";
                return PublishResult(completed: false);
            }

            replacement.Dispose();
            if (!projectManager.IsProjectLoadCurrent(this.stagedProject))
            {
                return PublishSuperseded();
            }

            var project = projectManager.AcceptProjectLoad(this.stagedProject);
            await sessions.CloseAsync().ConfigureAwait(true);
            opened = await sessions.OpenAsync(activationContext.ProjectContext, project).ConfigureAwait(true);
            await this.UpdateRecentProjectsAsync(context, operationId, diagnostics, CancellationToken.None)
                .ConfigureAwait(true);
            await this.UpdateCreationUsageAsync(request, operationId, diagnostics, CancellationToken.None)
                .ConfigureAwait(true);

            if (!await this.NavigateToWorkspaceAsync().ConfigureAwait(true))
            {
                await this.CloseIfCurrentAsync(opened).ConfigureAwait(true);
                diagnostics.Add(this.CreateDiagnostic(
                    operationId,
                    FailureDomain.WorkspaceActivation,
                    NavigationFailedCode,
                    DiagnosticSeverity.Error,
                    "Workspace activation failed.",
                    context.ProjectRoot.FullPath) with
                {
                    AffectedEntity = affectedScope,
                });
                return PublishResult(completed: false);
            }

            primaryGoalCompleted = true;
            title = request.Mode == ProjectActivationMode.CreateFromTemplate
                ? "Project created"
                : "Project opened";
            message = $"Workspace activated for '{context.Name}'.";
        }
        catch (OperationCanceledException ex)
        {
            wasCancelled = true;
            diagnostics.Add(
                exceptionAdapter.ToDiagnostic(
                    ex,
                    operationId,
                    FailureDomain.ProjectExplorer,
                    DiagnosticCodes.ProjectPrefix + "CANCELLED",
                    "Project activation was cancelled.",
                    request.ProjectLocation ?? request.ParentLocation));
            title = "Project activation cancelled";
            message = "The workspace was not activated.";
        }
        catch (Exception ex)
        {
            await this.CloseIfCurrentAsync(opened).ConfigureAwait(true);
            diagnostics.Add(
                exceptionAdapter.ToDiagnostic(
                    ex,
                    operationId,
                    FailureDomain.Unknown,
                    DiagnosticCodes.ProjectPrefix + "UNEXPECTED",
                    "Project activation failed unexpectedly.",
                    request.ProjectLocation ?? request.ParentLocation));
        }

        return PublishResult(primaryGoalCompleted);

        void ApplyFailureMessageFromDiagnostics()
        {
            var diagnostic = diagnostics.LastOrDefault(static item => item.Severity == DiagnosticSeverity.Error);
            if (diagnostic is null)
            {
                return;
            }

            message = string.IsNullOrWhiteSpace(diagnostic.TechnicalMessage)
                ? diagnostic.Message
                : $"{diagnostic.Message} {diagnostic.TechnicalMessage}";
        }

        OperationResult PublishSuperseded()
        {
            wasCancelled = true;
            title = "Project activation cancelled";
            message = "Another project became active while this project was loading; the active workspace was kept.";
            return PublishResult(completed: false);
        }

        OperationResult PublishResult(bool completed)
        {
            var resultDiagnostics = diagnostics.AsReadOnly();
            var result = new OperationResult
            {
                OperationId = operationId,
                OperationKind = operationKind,
                Status = statusReducer.Reduce(completed, wasCancelled, resultDiagnostics),
                Severity = statusReducer.ComputeSeverity(resultDiagnostics),
                Title = title,
                Message = message,
                StartedAt = startedAt,
                CompletedAt = DateTimeOffset.Now,
                AffectedScope = affectedScope,
                Diagnostics = resultDiagnostics,
                PrimaryAction = completed
                    ? null
                    : new PrimaryAction
                    {
                        ActionId = "open-details",
                        Label = "Details",
                        Kind = PrimaryActionKind.OpenDetails,
                    },
            };
            operationResults.Publish(result);
            return result;
        }
    }

    // The workspace window replaces the Project Explorer's.
    private Task<bool> NavigateToWorkspaceAsync()
        => EditorWindows.NavigateAsync(
            router,
            EditorWindows.WorkspacePage,
            new FullNavigation
            {
                Target = new Target { Name = EditorWindows.WorkspaceTarget },
                ReplaceTarget = true,
            });

    // Closes the session this activation opened, never one another activation opened since.
    private async Task CloseIfCurrentAsync(IProjectSession? opened)
    {
        if (opened is not null && ReferenceEquals(sessions.Current, opened))
        {
            await sessions.CloseAsync().ConfigureAwait(true);
        }
    }

    private async Task<ProjectContext?> OpenExistingProjectAsync(
        ProjectActivationRequest request,
        Guid operationId,
        List<DiagnosticRecord> diagnostics,
        CancellationToken cancellationToken)
    {
        if (string.IsNullOrWhiteSpace(request.ProjectLocation))
        {
            diagnostics.Add(this.CreateDiagnostic(
                operationId,
                FailureDomain.ProjectExplorer,
                InvalidRequestCode,
                DiagnosticSeverity.Error,
                "No project location was provided."));
            return null;
        }

        var saved = await projectManager.LoadProjectInfoAsync(request.ProjectLocation).ConfigureAwait(true);
        if (saved is not null)
        {
            try
            {
                await recovery.RecoverAsync(ProjectContext.FromProjectInfo(saved, fileSystem), cancellationToken).ConfigureAwait(true);
            }
            catch (Exception failure) when (failure is IOException or InvalidDataException or UnauthorizedAccessException or InvalidOperationException or System.Text.Json.JsonException)
            {
                diagnostics.Add(this.CreateDiagnostic(operationId, FailureDomain.ContentPipeline,
                    "Cook.RecoveryRequired", DiagnosticSeverity.Warning,
                    "Cooked content needs recovery. Authoring remains available: " + failure.Message, request.ProjectLocation));
            }
        }

        // Recovery can restore Project.oxy. Validate and load only the fresh manifest.
        var validationResult = await validation.ValidateAsync(request.ProjectLocation, cancellationToken)
            .ConfigureAwait(true);
        if (!validationResult.IsValid || validationResult.ProjectInfo is null)
        {
            diagnostics.Add(this.CreateValidationDiagnostic(operationId, validationResult));
            return null;
        }

        return await this.LoadProjectContextAsync(
                validationResult.ProjectInfo,
                operationId,
                diagnostics,
                openInitialScene: true,
                initialScene: null,
                cancellationToken)
            .ConfigureAwait(true);
    }

    [SuppressMessage(
        "Maintainability",
        "MA0051:Method is too long",
        Justification = "Creation failure mapping stays beside creation activation for the ED-M01 coordinator slice.")]
    private async Task<ProjectContext?> CreateProjectAsync(
        ProjectActivationRequest request,
        Guid operationId,
        List<DiagnosticRecord> diagnostics,
        CancellationToken cancellationToken)
    {
        if (string.IsNullOrWhiteSpace(request.TemplateLocation)
            || string.IsNullOrWhiteSpace(request.ProjectName)
            || string.IsNullOrWhiteSpace(request.ParentLocation)
            || request.Category is null)
        {
            diagnostics.Add(this.CreateDiagnostic(
                operationId,
                FailureDomain.ProjectTemplate,
                InvalidRequestCode,
                DiagnosticSeverity.Error,
                "Project creation request is incomplete.",
                request.ParentLocation));
            return null;
        }

        var creationResult = await creation.CreateFromTemplateAsync(
                new ProjectCreationRequest
                {
                    TemplateRoot = request.TemplateLocation,
                    ProjectName = request.ProjectName,
                    ParentLocation = request.ParentLocation,
                    Category = request.Category,
                    Thumbnail = request.Thumbnail,
                },
                cancellationToken)
            .ConfigureAwait(true);

        if (!creationResult.Succeeded || creationResult.ProjectInfo is null)
        {
            if (creationResult.Validation is { } validationResult)
            {
                diagnostics.Add(this.CreateValidationDiagnostic(operationId, validationResult));
            }

            if (creationResult.Exception is { } exception)
            {
                diagnostics.Add(
                    exceptionAdapter.ToDiagnostic(
                        exception,
                        operationId,
                        FailureDomain.ProjectTemplate,
                        CreationFailedCode,
                        creationResult.Message ?? "Project creation failed.",
                        creationResult.ProjectRoot));
            }
            else if (creationResult.Validation is null)
            {
                diagnostics.Add(this.CreateDiagnostic(
                    operationId,
                    FailureDomain.ProjectTemplate,
                    CreationFailedCode,
                    DiagnosticSeverity.Error,
                    creationResult.Message ?? "Project creation failed.",
                    creationResult.ProjectRoot));
            }

            return null;
        }

        return await this.LoadProjectContextAsync(
                creationResult.ProjectInfo,
                operationId,
                diagnostics,
                creationResult.OpenStarterScene,
                creationResult.StarterScene,
                cancellationToken)
            .ConfigureAwait(true);
    }

    private async Task<ProjectContext?> LoadProjectContextAsync(
        Oxygen.Editor.Projects.IProjectInfo projectInfo,
        Guid operationId,
        List<DiagnosticRecord> diagnostics,
        bool openInitialScene,
        AssetPath? initialScene,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();

        this.stagedProject = await projectManager.StageProjectLoadAsync(projectInfo, cancellationToken).ConfigureAwait(true);
        if (this.stagedProject is null)
        {
            diagnostics.Add(this.CreateDiagnostic(
                operationId,
                FailureDomain.ProjectPersistence,
                LoadFailedCode,
                DiagnosticSeverity.Error,
                "Project loaded its manifest but failed while loading project contents.",
                projectInfo.Location));
            return null;
        }

        return ProjectContext.FromProject(this.stagedProject.Project, fileSystem) with
        {
            OpenInitialScene = openInitialScene,
            InitialScene = initialScene,
        };
    }

    private async Task UpdateRecentProjectsAsync(
        ProjectContext context,
        Guid operationId,
        List<DiagnosticRecord> diagnostics,
        CancellationToken cancellationToken)
    {
        try
        {
            await recentProjects.RecordActivatedAsync(context, cancellationToken).ConfigureAwait(true);
        }
        catch (Exception ex) when (ex is not OperationCanceledException)
        {
            diagnostics.Add(
                exceptionAdapter.ToDiagnostic(
                    ex,
                    operationId,
                    FailureDomain.ProjectUsage,
                    RecentUsageWarningCode,
                    "The project opened, but recent-project usage could not be updated.",
                    context.ProjectRoot.FullPath,
                    new AffectedScope
                    {
                        ProjectId = context.ProjectId,
                        ProjectName = context.Name,
                        ProjectPath = context.ProjectRoot.FullPath,
                    }) with
                {
                    Severity = DiagnosticSeverity.Warning,
                });
        }
    }

    private async Task UpdateCreationUsageAsync(
        ProjectActivationRequest request,
        Guid operationId,
        List<DiagnosticRecord> diagnostics,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();

        if (request.Mode != ProjectActivationMode.CreateFromTemplate)
        {
            return;
        }

        if (!string.IsNullOrWhiteSpace(request.ParentLocation))
        {
            try
            {
                var settings = await projectExplorer.GetSettingsAsync().ConfigureAwait(true);
                settings.LastSaveLocation = request.ParentLocation;
                await projectExplorer.SaveSettingsAsync().ConfigureAwait(true);
            }
            catch (Exception ex) when (ex is not OperationCanceledException)
            {
                diagnostics.Add(
                    exceptionAdapter.ToDiagnostic(
                        ex,
                        operationId,
                        FailureDomain.ProjectSettings,
                        SettingsWarningCode,
                        "The project opened, but the last project location could not be updated.",
                        request.ParentLocation) with
                    {
                        Severity = DiagnosticSeverity.Warning,
                    });
            }
        }

        if (string.IsNullOrWhiteSpace(request.TemplateLocation))
        {
            return;
        }

        cancellationToken.ThrowIfCancellationRequested();
        try
        {
            await templateUsage.UpdateTemplateUsageAsync(request.TemplateLocation).ConfigureAwait(true);
        }
        catch (Exception ex) when (ex is not OperationCanceledException)
        {
            diagnostics.Add(
                exceptionAdapter.ToDiagnostic(
                    ex,
                    operationId,
                    FailureDomain.ProjectTemplate,
                    TemplateUsageWarningCode,
                    "The project opened, but template usage could not be updated.",
                    request.TemplateLocation) with
                {
                    Severity = DiagnosticSeverity.Warning,
                });
        }
    }

    [SuppressMessage(
        "Performance",
        "CA1822:Mark members as static",
        Justification = "Instance helper keeps diagnostic mapping grouped with coordinator dependencies.")]
    private DiagnosticRecord CreateValidationDiagnostic(
        Guid operationId,
        ProjectValidationResult validationResult)
    {
        var domain = validationResult.State == ProjectValidationState.InvalidContentRoots
            ? FailureDomain.ProjectContentRoots
            : FailureDomain.ProjectValidation;

        return this.CreateDiagnostic(
            operationId,
            domain,
            ValidationFailedCode,
            DiagnosticSeverity.Error,
            validationResult.Message ?? $"Project validation failed: {validationResult.State}.",
            validationResult.ProjectRoot,
            validationResult.Exception);
    }

    [SuppressMessage(
        "Performance",
        "CA1822:Mark members as static",
        Justification = "Instance helper keeps diagnostic mapping grouped with coordinator dependencies.")]
    private DiagnosticRecord CreateDiagnostic(
        Guid operationId,
        FailureDomain domain,
        string code,
        DiagnosticSeverity severity,
        string message,
        string? affectedPath = null,
        Exception? exception = null)
        => new()
        {
            OperationId = operationId,
            Domain = domain,
            Severity = severity,
            Code = code,
            Message = message,
            TechnicalMessage = exception?.Message,
            ExceptionType = exception?.GetType().FullName,
            AffectedPath = affectedPath,
        };
}
