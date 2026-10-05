// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using System.Globalization;
using System.Text.Json;
using Microsoft.Extensions.Logging;
using Oxygen.Editor.ContentPipeline.Cooking;
using Oxygen.Editor.ContentPipeline.Incremental;
using Oxygen.Managed.Core.Compatibility;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.ContentPipeline;

/// <summary>
/// Runs native content tools under owned process and artifact lifetimes.
/// </summary>
/// <param name="toolLocator">The native tool locator.</param>
/// <param name="processRunner">The contained worker runner.</param>
/// <param name="logger">The operation logger.</param>
/// <param name="nativeCompatibility">The operation-specific native compatibility check.</param>
public sealed partial class ImportToolContentPipelineApi(
    IEngineContentPipelineToolLocator toolLocator,
    IContentPipelineProcessRunner processRunner,
    ILogger<ImportToolContentPipelineApi> logger,
    INativeCompatibilityService nativeCompatibility) : IEngineContentPipelineApi
{
    private static readonly JsonSerializerOptions ManifestJsonOptions = new() { WriteIndented = true };
    private static readonly JsonSerializerOptions CaptureJsonOptions = new() { PropertyNamingPolicy = JsonNamingPolicy.SnakeCaseLower };

    private readonly IEngineContentPipelineToolLocator toolLocator = toolLocator ?? throw new ArgumentNullException(nameof(toolLocator));
    private readonly IContentPipelineProcessRunner processRunner = processRunner ?? throw new ArgumentNullException(nameof(processRunner));
    private readonly ILogger<ImportToolContentPipelineApi> logger = logger ?? throw new ArgumentNullException(nameof(logger));
    private readonly INativeCompatibilityService nativeCompatibility = nativeCompatibility;

    /// <inheritdoc />
    public async Task<NativeImportResult> ImportAsync(
        ContentImportExecution execution,
        CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(execution);
        ArgumentNullException.ThrowIfNull(execution.Manifest, nameof(execution));
        cancellationToken.ThrowIfCancellationRequested();
        ValidateExecutionPaths(execution);

        var compatibility = execution.Artifacts is { } borrowed
            ? new NativeCompatibilityResult(borrowed, [])
            : await this.nativeCompatibility.VerifyAsync(execution.OperationId, cancellationToken).ConfigureAwait(false);
        if (!compatibility.Succeeded)
        {
            return new(Succeeded: false, compatibility.Diagnostics);
        }

        var artifacts = compatibility.Artifacts!;
        var manifestPath = Path.Combine(execution.OperationRoot, "manifests", $"import-{Guid.NewGuid():N}.json");
        var reportPath = manifestPath + ".report.json";
        var capturePath = execution.CapturedInputs is null ? null : manifestPath + ".captures.json";
        string[] ownedPaths = capturePath is null ? [manifestPath, reportPath] : [manifestPath, reportPath, capturePath];
        Task? retainedWorkerDrain = null;

        try
        {
            return await this.RunImportWorkerAsync(execution, artifacts, manifestPath, reportPath, capturePath, cancellationToken).ConfigureAwait(false);
        }
        catch (ContentPipelineTerminationException ex)
        {
            retainedWorkerDrain = ReleaseAfterWorkerDrainAsync(ex.DrainCompletion, ownedPaths, execution.Artifacts is null ? artifacts : null);
            throw new ContentPipelineTerminationException(ex.InnerException ?? ex, retainedWorkerDrain);
        }
        finally
        {
            if (retainedWorkerDrain is null)
            {
                if (execution.Artifacts is null)
                {
                    await artifacts.DisposeAsync().ConfigureAwait(false);
                }

                foreach (var path in ownedPaths)
                {
                    TryDeleteFile(path);
                }
            }
        }
    }

    /// <inheritdoc />
    public async Task<CookInspectionResult> InspectLooseCookedRootAsync(string cookedRoot, CancellationToken cancellationToken)
    {
        try
        {
            var lease = await CookOutputReadLease.AcquireAsync(cookedRoot, cancellationToken).ConfigureAwait(false);
            await using var lifetime = lease.ConfigureAwait(false);
            var inventory = await lease.ReadInventoryAsync(this, cancellationToken).ConfigureAwait(false);
            return inventory.ToInspection(cookedRoot);
        }
        catch (Exception error) when (IsLooseCookedReadFailure(error))
        {
            return new(cookedRoot, Succeeded: false, SourceIdentity: null, [], [], [InventoryFailure(cookedRoot, error, ContentPipelineDiagnosticCodes.InspectFailed)]);
        }
    }

    /// <inheritdoc />
    public async Task<CookValidationResult> ValidateLooseCookedRootAsync(string cookedRoot, CancellationToken cancellationToken)
    {
        try
        {
            var lease = await CookOutputReadLease.AcquireAsync(cookedRoot, cancellationToken).ConfigureAwait(false);
            await using var lifetime = lease.ConfigureAwait(false);
            var inventory = await lease.ReadInventoryAsync(this, cancellationToken).ConfigureAwait(false);
            return inventory.ToValidation(cookedRoot);
        }
        catch (Exception error) when (IsLooseCookedReadFailure(error))
        {
            return new(cookedRoot, Succeeded: false, [InventoryFailure(cookedRoot, error, ContentPipelineDiagnosticCodes.ValidateFailed)]);
        }
    }

    private static DiagnosticRecord InventoryFailure(string root, Exception error, string code) => new()
    {
        OperationId = Guid.NewGuid(),
        Domain = FailureDomain.ContentPipeline, Severity = DiagnosticSeverity.Error,
        Code = code, Message = "Cooked inventory verification failed.",
        TechnicalMessage = error.Message, ExceptionType = error.GetType().FullName, AffectedPath = root,
    };

    private static async Task WriteManifestAsync(ContentImportManifest manifest, string path, CancellationToken cancellationToken)
    {
        var stream = File.Create(path);
        await using (stream.ConfigureAwait(false))
        {
            await JsonSerializer.SerializeAsync(stream, manifest, ManifestJsonOptions, cancellationToken).ConfigureAwait(false);
        }
    }

    private static ContentPipelineProcessRequest CreateImportRequest(string toolPath, string output, string manifestPath, string inputRoot)
        => new(
            toolPath,
            ["--no-tui", "--no-color", "--cooked-root", output, "batch", "--manifest", manifestPath, "--root", inputRoot],
            inputRoot);

    private static async Task WriteCapturedInputsAsync(Import.NativeCapturedInputSet inputs, string path, CancellationToken cancellationToken)
    {
        var capture = File.Create(path);
        await using (capture.ConfigureAwait(false))
        {
            await JsonSerializer.SerializeAsync(capture, inputs, CaptureJsonOptions, cancellationToken).ConfigureAwait(false);
        }
    }

    [SuppressMessage(
        "Design",
        "CA1031:Do not catch general exception types",
        Justification = "Best-effort cleanup must not mask the primary content-pipeline result.")]
    private static void TryDeleteFile(string path)
    {
        try
        {
            if (File.Exists(path))
            {
                File.Delete(path);
            }
        }
        catch (IOException)
        {
            // Best effort only.
        }
        catch (UnauthorizedAccessException)
        {
            // Best effort only.
        }
    }

    private static DiagnosticRecord CreateImportFailureDiagnostic(
        Guid operationId,
        ContentPipelineProcessResult result)
    {
        var technical = string.Join(
            Environment.NewLine,
            new[] { result.StandardError, result.StandardOutput }.Where(static text => !string.IsNullOrWhiteSpace(text)));

        return new DiagnosticRecord
        {
            OperationId = operationId,
            Domain = FailureDomain.AssetImport,
            Severity = DiagnosticSeverity.Error,
            Code = AssetImportDiagnosticCodes.ImportFailed,
            Message = "Native content import failed.",
            TechnicalMessage = string.IsNullOrWhiteSpace(technical) ? string.Create(CultureInfo.InvariantCulture, $"Exit code {result.ExitCode}.") : technical,
        };
    }

    private static void ValidateExecutionPaths(ContentImportExecution execution)
    {
        if (execution.OperationId == Guid.Empty
            || !Path.IsPathFullyQualified(execution.InputRoot)
            || !Path.IsPathFullyQualified(execution.OperationRoot)
            || !Path.IsPathFullyQualified(execution.Manifest.Output))
        {
            throw new ArgumentException("Native import requires an operation identity and absolute input, operation, and output paths.", nameof(execution));
        }
    }

    private static bool IsLooseCookedReadFailure(Exception ex)
        => ex is IOException
            or UnauthorizedAccessException
            or InvalidDataException
            or JsonException
            or System.ComponentModel.Win32Exception
            or NotSupportedException
            or InvalidOperationException
            or ArgumentException;

    [LoggerMessage(
        EventId = 1001,
        Level = LogLevel.Information,
        Message = "Invoking ImportTool '{ToolPath}' with manifest '{ManifestPath}' in '{WorkingDirectory}'.")]
    private partial void LogImportToolInvoked(string toolPath, string manifestPath, string workingDirectory);

    private async Task<NativeImportResult> RunImportWorkerAsync(ContentImportExecution execution, NativeArtifactLease artifacts, string manifestPath, string reportPath, string? capturePath, CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        var toolPath = this.GetCompatibleToolPath(artifacts);
        Directory.CreateDirectory(Path.GetDirectoryName(manifestPath)!);
        await WriteManifestAsync(execution.Manifest, manifestPath, cancellationToken).ConfigureAwait(false);
        var request = CreateImportRequest(toolPath, execution.Manifest.Output, manifestPath, execution.InputRoot) with
        {
            Output = CookRunContext.Current is { } progress ? new CookOutput(progress) : null,
        };

        request = request with { Arguments = [.. request.Arguments, "--report", reportPath] };
        if (capturePath is not null)
        {
            await WriteCapturedInputsAsync(execution.CapturedInputs!, capturePath, cancellationToken).ConfigureAwait(false);
            request = request with { Arguments = [.. request.Arguments, "--captured-inputs", capturePath] };
        }

        this.LogImportToolInvoked(toolPath, manifestPath, execution.InputRoot);
        var result = await this.processRunner.RunAsync(request, cancellationToken).ConfigureAwait(false);
        return File.Exists(reportPath)
            ? Import.NativeImportReportReader.Read(await File.ReadAllTextAsync(reportPath, cancellationToken).ConfigureAwait(false), execution, result.ExitCode)
            : result.ExitCode == 0
            ? throw new InvalidDataException("Native source import did not produce its required output report.")
            : new NativeImportResult(Succeeded: false, Diagnostics: [CreateImportFailureDiagnostic(execution.OperationId, result)]);
    }

    private sealed class CookOutput(IProgress<CookRunProgress> progress) : IProgress<ContentPipelineProcessOutput>
    {
        public void Report(ContentPipelineProcessOutput value) => progress.Report(new(Message: value.Text));
    }
}
