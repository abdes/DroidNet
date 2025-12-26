// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json;

namespace Oxygen.Assets.Import;

public static class ImportSettingsHelper
{
    private static readonly JsonSerializerOptions JsonOptions = new() { PropertyNameCaseInsensitive = true };

    public static async Task<Dictionary<string, string>> GetEffectiveSettingsAsync(ImportContext context, CancellationToken cancellationToken)
    {
        var settings = context.Input.Settings != null
            ? new Dictionary<string, string>(context.Input.Settings, StringComparer.OrdinalIgnoreCase)
            : new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);

        var sidecarPath = context.Input.SourcePath + ".import.json";
        try
        {
            var bytes = await context.Files.ReadAllBytesAsync(sidecarPath, cancellationToken).ConfigureAwait(false);
            var sidecar = JsonSerializer.Deserialize<SidecarData>(bytes.Span, JsonOptions);

            if (sidecar?.Settings != null)
            {
                foreach (var (key, value) in sidecar.Settings)
                {
                    settings[key] = value;
                }
            }
        }
        catch (FileNotFoundException)
        {
            // No sidecar, ignore
        }
        catch (JsonException)
        {
            context.Diagnostics.Add(
                ImportDiagnosticSeverity.Warning,
                code: "OXYIMPORT_SIDECAR_INVALID",
                message: "Failed to parse sidecar JSON.",
                sourcePath: sidecarPath);
        }

        return settings;
    }

    private sealed record SidecarData(Dictionary<string, string>? Settings);
}
