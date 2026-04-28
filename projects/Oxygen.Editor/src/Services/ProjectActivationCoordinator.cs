// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using DroidNet.Routing;
using Oxygen.Core.Diagnostics;
using Oxygen.Editor.Data.Services;
using Oxygen.Editor.ProjectBrowser.Activation;
using Oxygen.Editor.ProjectBrowser.Projects;
using Oxygen.Editor.Projects;

namespace Oxygen.Editor.Services;

/// <summary>
/// Host-owned coordinator for project open/create and workspace activation.
/// </summary>
public sealed class ProjectActivationCoordinator(
    IRouter router,
    IProjectValidationService validation,
    IProjectCreationService creation,
    IProjectManagerService projectManager,
    IProjectContextService projectContextService,
    IRecentProjectAdapter recentProjects,
    ITemplateUsageService templateUsage,
    IProjectBrowserService projectBrowser,
    IOperationResultPublisher operationResults,
    IStatusReducer statusReducer,
    IExceptionDiagnosticAdapter exceptionAdapter) : IProjectActivationCoordinator, IDisposable
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
                ProjectPath = context.ProjectRoot,
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

            projectContextService.Activate(activationContext.ProjectContext);
            await this.UpdateRecentProjectsAsync(context, operationId, diagnostics, cancellationToken)
                .ConfigureAwait(true);
            await this.UpdateCreationUsageAsync(request, operationId, diagnostics, cancellationToken)
                .ConfigureAwait(true);

            try
            {
                await router.NavigateAsync(
                        "/we",
                        new FullNavigation
                        {
                            Target = new Target { Name = "wnd-we" },
                            ReplaceTarget = true,
                        })
                    .ConfigureAwait(true);
            }
            catch (Exception ex)
            {
                projectContextService.Close();
                diagnostics.Add(
                    exceptionAdapter.ToDiagnostic(
                        ex,
                        operationId,
                        FailureDomain.WorkspaceActivation,
                        NavigationFailedCode,
                        "Workspace activation failed.",
                        context.ProjectRoot,
                        affectedScope));
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
                    FailureDomain.ProjectBrowser,
                    DiagnosticCodes.ProjectPrefix + "CANCELLED",
                    "Project activation was cancelled.",
                    request.ProjectLocation ?? request.ParentLocation));
            title = "Project activation cancelled";
            message = "The workspace was not activated.";
        }
        catch (Exception ex)
        {
            projectContextService.Close();
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
                FailureDomain.ProjectBrowser,
                InvalidRequestCode,
                DiagnosticSeverity.Error,
                "No project location was provided."));
            return null;
        }

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
                cancellationToken)
            .ConfigureAwait(true);
    }

    private async Task<ProjectContext?> LoadProjectContextAsync(
        Oxygen.Editor.World.IProjectInfo projectInfo,
        Guid operationId,
        List<DiagnosticRecord> diagnostics,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();

        if (!await projectManager.LoadProjectAsync(projectInfo).ConfigureAwait(true)
            || projectManager.CurrentProject is null)
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

        return ProjectContext.FromProject(projectManager.CurrentProject);
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
                    context.ProjectRoot,
                    new AffectedScope
                    {
                        ProjectId = context.ProjectId,
                        ProjectName = context.Name,
                        ProjectPath = context.ProjectRoot,
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
                var settings = await projectBrowser.GetSettingsAsync().ConfigureAwait(true);
                settings.LastSaveLocation = request.ParentLocation;
                await projectBrowser.SaveSettingsAsync().ConfigureAwait(true);
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
