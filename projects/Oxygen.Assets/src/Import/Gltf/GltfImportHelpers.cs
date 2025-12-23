// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json;
using Oxygen.Assets.Filesystem;

namespace Oxygen.Assets.Import.Gltf;

internal static class GltfImportHelpers
{
    internal static string GetSidecarPath(string sourcePath) => sourcePath + ".import.json";

    internal static string ResolveRelativeToSource(string sourcePath, string relative)
    {
        var dir = VirtualPath.GetDirectory(sourcePath);
        return string.IsNullOrEmpty(dir)
             ? VirtualPath.NormalizeSlashes(relative)
             : VirtualPath.Combine(dir, relative);
    }

    internal static (List<string> safeRelativeUris, List<string> unsafeUris) EnumerateGltfReferencedResources(ReadOnlyMemory<byte> jsonUtf8)
    {
        var safeRelativeUris = new List<string>();
        var unsafeUris = new List<string>();

        try
        {
            using var doc = JsonDocument.Parse(jsonUtf8);

            AddUrisFromArray(doc.RootElement, "buffers");
            AddUrisFromArray(doc.RootElement, "images");
        }
        catch (JsonException)
        {
            // Keep behavior: invalid JSON yields no dependencies and no materialized resources.
        }

        return (safeRelativeUris, unsafeUris);

        void AddUrisFromArray(JsonElement root, string arrayPropertyName)
        {
            if (!root.TryGetProperty(arrayPropertyName, out var elements) || elements.ValueKind != JsonValueKind.Array)
            {
                return;
            }

            foreach (var element in elements.EnumerateArray())
            {
                if (!element.TryGetProperty("uri", out var uriProperty) || uriProperty.ValueKind != JsonValueKind.String)
                {
                    continue;
                }

                var uri = uriProperty.GetString();
                if (!IsExternalUri(uri))
                {
                    continue;
                }

                if (TryGetSafeRelativeUri(uri!, out var safeRelative))
                {
                    safeRelativeUris.Add(safeRelative);
                }
                else
                {
                    unsafeUris.Add(uri!);
                }
            }
        }

        static bool IsExternalUri(string? uri)
            => !string.IsNullOrWhiteSpace(uri)
               && !uri.StartsWith("data:", StringComparison.OrdinalIgnoreCase)
               && !uri.Contains("://", StringComparison.Ordinal);

        static bool TryGetSafeRelativeUri(string uri, out string safeRelative)
        {
            safeRelative = string.Empty;

            if (string.IsNullOrWhiteSpace(uri))
            {
                return false;
            }

            var s = VirtualPath.NormalizeSlashes(uri).Trim();

            // We only support external relative file references.
            if (s.StartsWith("data:", StringComparison.OrdinalIgnoreCase)
                || s.Contains("://", StringComparison.Ordinal)
                || s.StartsWith('/'))
            {
                return false;
            }

            // Reject Windows drive letters / absolute paths and other colon-based schemes.
            if (s.Contains(':', StringComparison.Ordinal))
            {
                return false;
            }

            safeRelative = s;
            return true;
        }
    }
}
