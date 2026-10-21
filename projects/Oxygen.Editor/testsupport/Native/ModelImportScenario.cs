// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Automation.Peers;
using Microsoft.UI.Xaml.Automation.Provider;
using Microsoft.UI.Xaml.Controls;
using Oxygen.Editor.ContentPipeline;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.TestSupport;

internal sealed class ModelImportScenario : DroidNet.Tests.VisualUserInterfaceTests
{
    internal static void InvokeImportButton(Button button)
    {
        _ = button.IsEnabled.Should().BeTrue();
        _ = button.Focus(FocusState.Programmatic);
        var peer = FrameworkElementAutomationPeer.FromElement(button) ?? new ButtonAutomationPeer(button);
        ((IInvokeProvider)peer.GetPattern(PatternInterface.Invoke)).Invoke();
    }

    internal static async Task<ContentCookResult> ImportTypedModelAsync(NativeSceneFixture fixture, CatalogWorkloadServices services, string format, CancellationToken cancellationToken)
    {
        var path = await WriteTypedModelSourceAsync(fixture, format, cancellationToken).ConfigureAwait(true);
        var result = await services.Pipeline.ImportSourceAsync(new(services.Session.Context, path, "Triangle", AssetPath.Parse("/Content/Models")), cancellationToken).ConfigureAwait(true);
        _ = result.IsPublished.Should().BeTrue(string.Join(Environment.NewLine, result.Diagnostics.Select(static issue => issue.Message)));
        _ = result.CookedAssets.Should().Contain(asset => asset.Kind == ContentCookAssetKind.Scene);
        return result;
    }

    internal static async Task<string> WriteTypedModelSourceAsync(NativeSceneFixture fixture, string format, CancellationToken cancellationToken)
    {
        var path = fixture.ProjectRoot.Combine("Incoming").Combine("Triangle." + format).FullPath;
        _ = Directory.CreateDirectory(Path.GetDirectoryName(path)!);
        var source = await File.ReadAllTextAsync(Path.Combine(AppContext.BaseDirectory, "Fixtures/static_textured_triangle." + format), cancellationToken).ConfigureAwait(true);
        if (string.Equals(format, "fbx", StringComparison.Ordinal))
        {
            source = source.Replace("\"DiffuseColor\", \"Color\", \"\", \"A\",1,1,1", "\"DiffuseColor\", \"Color\", \"\", \"A\",0.8,0.2,0.1", StringComparison.Ordinal);
        }

        File.Copy(Path.Combine(AppContext.BaseDirectory, "Fixtures/static_textured_checker.png"), Path.Combine(Path.GetDirectoryName(path)!, "static_textured_checker.png"));
        await File.WriteAllTextAsync(path, source, cancellationToken).ConfigureAwait(true);
        return path;
    }

    internal static async Task<ContentCookResult> CreateImportedLibraryAsync(NativeSceneFixture consumer, CatalogWorkloadServices services, string format, CancellationToken cancellationToken)
    {
        var producer = new NativeSceneFixture(automatic: false);
        await using var lifetime = producer.ConfigureAwait(true);
        using var producerServices = new CatalogWorkloadServices(producer);
        var imported = await ImportTypedModelAsync(producer, producerServices, format, cancellationToken).ConfigureAwait(true);
        var source = await producer.GetCookedRootAsync(producer.ProjectRoot, cancellationToken).ConfigureAwait(true);
        var library = consumer.ProjectRoot.Combine("Library");
        foreach (var path in Directory.GetFiles(source.FullPath, "*", SearchOption.AllDirectories))
        {
            if (string.Equals(Path.GetFileName(path), ".generation.lock", StringComparison.Ordinal))
            {
                continue;
            }

            var target = Path.Combine(library.FullPath, Path.GetRelativePath(source.FullPath, path));
            _ = Directory.CreateDirectory(Path.GetDirectoryName(target)!);
            File.Copy(path, target);
        }

        var next = services.Session.Context with
        {
            LocalFolderMounts = [new("Library", library)],
        };
        var manager = new Oxygen.Editor.Projects.ProjectManagerService(services.Storage);
        var saved = await manager.LoadProjectInfoAsync(consumer.ProjectRoot.FullPath).ConfigureAwait(true) ?? throw new InvalidDataException("The consumer project must be saved.");
        var updated = new Oxygen.Editor.Projects.ProjectInfo(next.ProjectId, next.Name, next.Category, next.ProjectRoot.FullPath, next.Thumbnail)
        {
            AuthoringMounts = [.. next.AuthoringMounts],
            LocalFolderMounts = [.. next.LocalFolderMounts],
            CookedContentOrder = [.. next.CookedContentOrder],
        };
        await manager.SaveProjectInfoAsync(updated, saved, cancellationToken).ConfigureAwait(true);
        services.Sessions.ChangeContext(next);
        return imported;
    }
}
