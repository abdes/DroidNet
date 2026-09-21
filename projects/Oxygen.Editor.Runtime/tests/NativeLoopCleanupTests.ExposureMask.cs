// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Microsoft.Extensions.Logging.Abstractions;
using Oxygen.Editor.ContentPipeline;
using Oxygen.Editor.Runtime.Engine;

namespace Oxygen.Editor.Runtime.Tests;

public sealed partial class NativeLoopCleanupTests
{
    /// <summary>A cooked texture reaches the live scene through the public native boundary.</summary>
    /// <returns>The native cooking and asynchronous loading check.</returns>
    [TestMethod]
    public async Task CookedExposureMaskAppliesCompleteRevisionAndClears()
    {
        var directory = Directory.CreateTempSubdirectory("oxygen-live-mask-");
        try
        {
            var cookedRoot = await this.CookExposureMaskAsync(directory.FullName).ConfigureAwait(false);
            var descriptor = Directory.EnumerateFiles(cookedRoot, "*.otex", SearchOption.AllDirectories).Single();
            var reference = new RuntimeTextureReference(
                new Uri("asset:///Content/Textures/Meter.otex.json"),
                cookedRoot,
                Path.GetRelativePath(cookedRoot, descriptor));
            await this.RunNativeCommandsAsync(commands => this.CheckCookedExposureMaskAsync(commands, reference), [cookedRoot]).ConfigureAwait(false);
        }
        finally
        {
            directory.Delete(recursive: true);
        }
    }

    private async Task<string> CookExposureMaskAsync(string root)
    {
        // A one-pixel, uncompressed 32-bit TGA. Native cooking owns all Oxygen
        // binary records; this fixture supplies only an ordinary source image.
        byte[] image = [0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 1, 0, 32, 8, 255, 255, 255, 255];
        await File.WriteAllBytesAsync(Path.Combine(root, "Meter.tga"), image, this.TestContext.CancellationToken).ConfigureAwait(false);
        const string descriptor = """
            { "source": "Meter.tga", "intent": "data",
              "decode": { "color_space": "linear" }, "output": { "format": "rgba8" } }
            """;
        await File.WriteAllTextAsync(Path.Combine(root, "Meter.otex.json"), descriptor, this.TestContext.CancellationToken).ConfigureAwait(false);
        var cookedRoot = Path.Combine(root, "cooked");
        var manifest = new ContentImportManifest(
            1,
            cookedRoot,
            new ContentImportLayout("/Content") { TextureDescriptorsDirectory = "Textures" },
            [new ContentImportJob("mask", "texture-descriptor", "Meter.otex.json", [], Output: null, Name: "Meter")]);
        using var compatibility = Oxygen.Testing.TemporaryNativeArtifacts.ForInstalledEngine();
        var api = new ImportToolContentPipelineApi(
            new EngineContentPipelineToolLocator(),
            new ContentPipelineProcessRunner(),
            NullLogger<ImportToolContentPipelineApi>.Instance,
            compatibility);
        var execution = new ContentImportExecution(Guid.NewGuid(), root, Path.Combine(root, "operation"), manifest);
        var result = await api.ImportAsync(execution, this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = result.Succeeded.Should().BeTrue(string.Join(Environment.NewLine, result.Diagnostics.Select(static diagnostic => diagnostic.TechnicalMessage ?? diagnostic.Message)));
        return cookedRoot;
    }

    private async Task CheckCookedExposureMaskAsync(RuntimeCommandDispatcher commands, RuntimeTextureReference reference)
    {
        var target = new RuntimeSceneTarget(commands.RunId, Guid.NewGuid(), Guid.NewGuid(), Guid.NewGuid());
        _ = (await commands.ActivateSceneAsync(Guid.NewGuid(), target, "Cooked mask", this.TestContext.CancellationToken).ConfigureAwait(false)).Succeeded.Should().BeTrue();
        var initial = EnvironmentRequest(ObservedEnvironmentValues());
        _ = commands.Execute(new RuntimeWorldRequest(Guid.NewGuid(), target, initial), this.TestContext.CancellationToken).Succeeded.Should().BeTrue();
        var masked = initial with { ManualExposureEv = 6f, AutoExposureMeteringMask = reference };
        var request = new RuntimeWorldRequest(Guid.NewGuid(), target, masked);
        _ = commands.Execute(request, this.TestContext.CancellationToken).Succeeded.Should().BeTrue();
        await this.WaitForAppliedAssetAsync(commands, request).ConfigureAwait(false);

        var observed = await commands.ObserveEnvironmentAsync(Guid.NewGuid(), target, this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = observed.Outcome.Succeeded.Should().BeTrue();
        _ = observed.State!.AutoExposureMeteringMask.Should().BePositive();
        _ = observed.State.ExposureMaskPending.Should().BeFalse();
        _ = observed.State.ExposureMaskError.Should().BeEmpty();
        var expected = ObservedEnvironmentValues() with
        {
            ManualExposureEv = masked.ManualExposureEv,
            AutoExposureMeteringMask = observed.State.AutoExposureMeteringMask,
        };
        _ = observed.State.Should().BeEquivalentTo(expected, options => options.WithStrictOrdering());

        var clear = new RuntimeWorldRequest(Guid.NewGuid(), target, initial);
        _ = commands.Execute(clear, this.TestContext.CancellationToken).Succeeded.Should().BeTrue();
        await this.WaitForAppliedAssetAsync(commands, clear).ConfigureAwait(false);
        observed = await commands.ObserveEnvironmentAsync(Guid.NewGuid(), target, this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = observed.State.Should().BeEquivalentTo(ObservedEnvironmentValues(), options => options.WithStrictOrdering());
    }
}
