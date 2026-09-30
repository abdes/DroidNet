// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Editor.ContentPipeline;

namespace Oxygen.Editor.World.Tests;

/// <summary>Produces real named imported assets and independent cooked-only libraries.</summary>
public sealed partial class InspectorControlTests
{
    private static async Task<ContentCookResult> ImportTypedModelAsync(NativeSceneFixture fixture, CatalogWorkloadServices services, string format, CancellationToken cancellationToken)
    {
        var path = await WriteTypedModelSourceAsync(fixture, format, cancellationToken).ConfigureAwait(true);
        var result = await services.Pipeline.ImportSourceAsync(new(services.Projects.ActiveProject!, path, "Triangle", new("asset:///Content/Models")), cancellationToken).ConfigureAwait(true);
        _ = result.IsPublished.Should().BeTrue(string.Join(Environment.NewLine, result.Diagnostics.Select(static issue => issue.Message)));
        _ = result.CookedAssets.Should().Contain(asset => asset.Kind == ContentCookAssetKind.Scene);
        return result;
    }

    private static async Task<string> WriteTypedModelSourceAsync(NativeSceneFixture fixture, string format, CancellationToken cancellationToken)
    {
        var path = Path.Combine(fixture.ProjectRoot, "Incoming", "Triangle." + format);
        _ = Directory.CreateDirectory(Path.GetDirectoryName(path)!);
        var source = await File.ReadAllTextAsync(Path.Combine(AppContext.BaseDirectory, "Fixtures/static_scalar_triangle." + format), cancellationToken).ConfigureAwait(true);
        if (string.Equals(format, "fbx", StringComparison.Ordinal))
        {
            const string materialBlock = """

                    Material: 1007, "Material::Scalar", "" {
                        Version: 102
                        ShadingModel: "lambert"
                        Properties70: {
                            P: "DiffuseColor", "Color", "", "A",0.8,0.2,0.1
                            P: "DiffuseFactor", "Number", "", "A",1
                        }
                    }
                }
                Connections:
                """;
            source = source.Replace("\r\n", "\n", StringComparison.Ordinal)
                .Replace("\n}\nConnections:", materialBlock, StringComparison.Ordinal)
                .Replace("C: \"OO\",1001,1002", "C: \"OO\",1001,1002\n    C: \"OO\",1007,1002", StringComparison.Ordinal);
        }

        await File.WriteAllTextAsync(path, source, cancellationToken).ConfigureAwait(true);
        return path;
    }

    private static async Task<ContentCookResult> CreateImportedLibraryAsync(NativeSceneFixture consumer, CatalogWorkloadServices services, string format, CancellationToken cancellationToken)
    {
        var producer = new NativeSceneFixture(automatic: false);
        await using var lifetime = producer.ConfigureAwait(true);
        using var producerServices = new CatalogWorkloadServices(producer);
        var imported = await ImportTypedModelAsync(producer, producerServices, format, cancellationToken).ConfigureAwait(true);
        var source = await producer.GetCookedRootAsync(producer.ProjectRoot, cancellationToken).ConfigureAwait(true);
        var library = Path.Combine(consumer.ProjectRoot, "Library");
        foreach (var path in Directory.GetFiles(source, "*", SearchOption.AllDirectories))
        {
            if (string.Equals(Path.GetFileName(path), ".generation.lock", StringComparison.Ordinal))
            {
                continue;
            }

            var target = Path.Combine(library, Path.GetRelativePath(source, path));
            _ = Directory.CreateDirectory(Path.GetDirectoryName(target)!);
            File.Copy(path, target);
        }

        var next = services.Projects.ActiveProject! with { LocalFolderMounts = [new("Library", library)] };
        var manager = new Oxygen.Editor.Projects.ProjectManagerService(services.Storage);
        var saved = await manager.LoadProjectInfoAsync(consumer.ProjectRoot).ConfigureAwait(true)
            ?? throw new InvalidDataException("The consumer project must be saved.");
        var updated = new Oxygen.Editor.Projects.ProjectInfo(next.ProjectId, next.Name, next.Category, next.ProjectRoot, next.Thumbnail)
        {
            AuthoringMounts = [.. next.AuthoringMounts], LocalFolderMounts = [.. next.LocalFolderMounts], CookedContentOrder = [.. next.CookedContentOrder],
        };
        await manager.SaveProjectInfoAsync(updated, saved, cancellationToken).ConfigureAwait(true);
        services.Projects.Activate(next);
        return imported;
    }
}
