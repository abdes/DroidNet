// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json;

namespace Oxygen.Editor.ContentPipeline.TestSupport;

internal sealed class CapturingRunner(ContentPipelineProcessResult result) : IContentPipelineProcessRunner
{
    public ContentPipelineProcessRequest? Request { get; private set; }

    public string? ManifestJson { get; private set; }

    public string? ManifestPath { get; private set; }

    public async Task<ContentPipelineProcessResult> RunAsync(
        ContentPipelineProcessRequest request,
        CancellationToken cancellationToken)
    {
        this.Request = request;
        var manifestFlagIndex = request.Arguments.ToList().IndexOf("--manifest");
        var manifestPath = request.Arguments[manifestFlagIndex + 1];
        this.ManifestPath = manifestPath;
        this.ManifestJson = await File.ReadAllTextAsync(manifestPath, cancellationToken).ConfigureAwait(false);
        using var manifest = JsonDocument.Parse(this.ManifestJson);
        if (result.ExitCode == 0)
        {
            var reportPath = request.Arguments[request.Arguments.ToList().IndexOf("--report") + 1];
            var report = JsonSerializer.Serialize(new
            {
                schema_version = 6,
                session = new
                {
                    id = "session",
                    started_utc = "2026-01-01T00:00:00Z",
                    ended_utc = "2026-01-01T00:00:00Z",
                    tool_version = "1.0",
                    command_line = "tool batch",
                    cooked_root = request.Arguments[request.Arguments.ToList().IndexOf("--cooked-root") + 1],
                },
                summary = new
                {
                    jobs_total = manifest.RootElement.GetProperty("jobs").GetArrayLength(),
                    jobs_succeeded = manifest.RootElement.GetProperty("jobs").GetArrayLength(),
                    jobs_failed = 0,
                    jobs_skipped = 0,
                    jobs_cancelled = 0,
                    time_ms_total = 0.0,
                    time_ms_io = 0.0,
                    time_ms_cpu = 0.0,
                },
                jobs = manifest.RootElement.GetProperty("jobs").EnumerateArray().Select(static (job, index) => new
                {
                    index = index + 1,
                    type = job.GetProperty("type").GetString(),
                    work_items = Array.Empty<object>(),
                    stats = new
                    {
                        time_ms_total = 0.0,
                        time_ms_io = 0.0,
                        time_ms_source_load = 0.0,
                        time_ms_decode = 0.0,
                        time_ms_load = 0.0,
                        time_ms_cook = 0.0,
                        time_ms_emit = 0.0,
                        time_ms_finalize = 0.0,
                    },
                    status = "succeeded",
                    outputs = Array.Empty<object>(),
                    products = Array.Empty<object>(),
                    diagnostics = Array.Empty<object>(),
                }),
            });
            await File.WriteAllTextAsync(reportPath, report, cancellationToken).ConfigureAwait(false);
        }

        return result;
    }
}
