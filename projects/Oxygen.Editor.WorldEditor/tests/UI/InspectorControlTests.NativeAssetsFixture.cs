// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Editor.ContentBrowser.AssetIdentity;
using Oxygen.Editor.ContentBrowser.Materials;
using Oxygen.Managed.Assets.Persistence.LooseCooked.V2;

namespace Oxygen.Editor.World.Tests;

/// <summary>Cooks real material descriptors for native asset-assignment tests.</summary>
public sealed partial class InspectorControlTests
{
    private sealed partial class NativeSceneFixture
    {
        public async Task<(Uri uri, string key)> CookTestMaterialAsync(string name, CancellationToken cancellationToken, System.Numerics.Vector4? baseColor = null, string? projectRoot = null)
        {
            var root = projectRoot ?? this.ProjectRoot;
            var relative = $"Content/Materials/{name}.omat.json";
            var path = Path.Combine(root, relative.Replace('/', Path.DirectorySeparatorChar));
            _ = Directory.CreateDirectory(Path.GetDirectoryName(path)!);
            var color = baseColor ?? System.Numerics.Vector4.One;
            var factor = System.Text.Json.JsonSerializer.Serialize(new[] { color.X, color.Y, color.Z, color.W });
            var source = $$"""
                { "name": "{{name}}",
                  "parameters": { "base_color": {{factor}}, "metalness": 0, "roughness": 0.5 } }
                """;
            await File.WriteAllTextAsync(path, source, cancellationToken).ConfigureAwait(true);
            var native = this.MaterialPipelineFor(root);
            var uri = new Uri($"asset:///{relative}");
            var result = await native.Pipeline.CookAssetAsync(uri, cancellationToken).ConfigureAwait(true);
            _ = result.IsPublished.Should().BeTrue(string.Join(Environment.NewLine, result.Diagnostics.Select(static issue => issue.TechnicalMessage ?? issue.Message)));
            using var indexStream = File.OpenRead(Path.Combine(root, ".cooked", "Content", "container.index.bin"));
            var asset = LooseCookedIndex.Read(indexStream).Assets.Single(value => value.VirtualPath == $"/Content/Materials/{name}.omat");
            if (projectRoot is null)
            {
                var row = new MaterialPickerResult(uri, name, AssetState.Descriptor, AssetState.Cooked, AssetRuntimeAvailability.Mounted, path, Path.Combine(root, ".cooked", "Content", "Materials", $"{name}.omat"), BaseColorPreview: null);
                this.SetMaterialChoices(this.materialChoices.Value.Append(row).ToArray());
            }

            var keyBytes = new byte[16];
            asset.AssetKey.WriteBytes(keyBytes);
            return (uri, new Guid(keyBytes, bigEndian: true).ToString());
        }
    }
}
