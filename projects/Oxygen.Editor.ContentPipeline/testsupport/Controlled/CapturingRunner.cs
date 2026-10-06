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
                report_version = "2",
                session = new { cooked_root = manifest.RootElement.GetProperty("output").GetString() },
                jobs = manifest.RootElement.GetProperty("jobs").EnumerateArray().Select(static (job, index) => new
                {
                    index = index + 1,
                    type = job.GetProperty("type").GetString(),
                    status = "succeeded",
                    outputs = Array.Empty<object>(),
                    diagnostics = Array.Empty<object>(),
                }),
            });
            await File.WriteAllTextAsync(reportPath, report, cancellationToken).ConfigureAwait(false);
        }

        return result;
    }
}
