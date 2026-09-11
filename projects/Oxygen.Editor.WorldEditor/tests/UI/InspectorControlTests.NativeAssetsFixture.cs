// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Editor.ContentBrowser.AssetIdentity;
using Oxygen.Editor.ContentBrowser.Materials;
using Oxygen.Managed.Assets.Import;
using Oxygen.Managed.Assets.Import.Materials;

namespace Oxygen.Editor.World.Tests;

/// <summary>Cooks real material descriptors for native asset-assignment tests.</summary>
public sealed partial class InspectorControlTests
{
    private sealed partial class NativeSceneFixture
    {
        public async Task<(Uri uri, string key)> CookTestMaterialAsync(string name, CancellationToken cancellationToken)
        {
            var relative = $"Content/Materials/{name}.omat.json";
            var path = Path.Combine(this.ProjectRoot, relative.Replace('/', Path.DirectorySeparatorChar));
            _ = Directory.CreateDirectory(Path.GetDirectoryName(path)!);
            var source = $$"""
                { "Schema": "oxygen.material.v1", "Type": "PBR", "Name": "{{name}}" }
                """;
            await File.WriteAllTextAsync(path, source, cancellationToken).ConfigureAwait(true);
            var registry = new ImporterRegistry();
            registry.Register(new MaterialSourceImporter());
            var importer = new ImportService(registry);
            var result = await importer.ImportAsync(
                new ImportRequest(this.ProjectRoot, [new ImportInput(relative, "Content")], new ImportOptions(FailFast: true)),
                cancellationToken).ConfigureAwait(true);
            _ = result.Succeeded.Should().BeTrue("the material fixture must be cooked by the production importer");
            var asset = result.Imported.Should().ContainSingle().Which;
            var uri = new Uri($"asset:///{relative}");
            var row = new MaterialPickerResult(uri, name, AssetState.Descriptor, AssetState.Cooked, AssetRuntimeAvailability.Mounted, path, Path.Combine(this.ProjectRoot, ".cooked", "Content", "Materials", $"{name}.omat"), BaseColorPreview: null);
            this.SetMaterialChoices(this.materialChoices.Value.Append(row).ToArray());
            var keyBytes = new byte[16];
            asset.AssetKey.WriteBytes(keyBytes);
            return (uri, new Guid(keyBytes, bigEndian: true).ToString());
        }
    }
}
