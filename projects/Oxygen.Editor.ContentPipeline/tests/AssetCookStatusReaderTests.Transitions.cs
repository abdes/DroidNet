// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json.Nodes;
using AwesomeAssertions;
using Moq;
using Oxygen.Editor.ContentPipeline.Status;
using Oxygen.Managed.Core.Compatibility;

namespace Oxygen.Editor.ContentPipeline.Tests;

/// <summary>Exercises freshness changes during inspection and native producer changes.</summary>
public sealed partial class AssetCookStatusReaderTests
{
    /// <summary>Unchanged geometry becomes out of date when its material changes or loses published bytes.</summary>
    /// <param name="damageOutput">Whether to damage output instead of changing material source.</param>
    /// <returns>The asynchronous dependency regression.</returns>
    [TestMethod]
    [DataRow(false)]
    [DataRow(true)]
    public async Task DependencyChangesAffectConsumerStatus(bool damageOutput)
    {
        using var project = new StatusProject();
        var catalog = JsonNode.Parse(await File.ReadAllTextAsync(Path.Combine(AppContext.BaseDirectory, "Fixtures", "BuiltinGeometryCatalog.json"), this.TestContext.CancellationToken).ConfigureAwait(false))!;
        var geometry = catalog["geometries"]![0]!["descriptor"]!;
        geometry["lods"]![0]!["submeshes"]![0]!["material_ref"] = "/Content/Material.omat";
        var uri = new Uri("asset:///Content/Geometry.ogeo.json");
        await File.WriteAllTextAsync(Path.Combine(project.Root, "Content", "Geometry.ogeo.json"), geometry.ToJsonString(), this.TestContext.CancellationToken).ConfigureAwait(false);
        await project.PublishAsync(this.TestContext.CancellationToken, uri).ConfigureAwait(false);
        var reader = project.CreateReader();
        var before = (await reader.ReadAsync(project.Project, [uri], this.TestContext.CancellationToken).ConfigureAwait(false)).Single();
        _ = before.Freshness.Should().Be(AssetCookFreshness.Current);
        if (damageOutput)
        {
            File.Delete(project.OutputPath);
        }
        else
        {
            await File.WriteAllTextAsync(project.SourcePath, StatusProject.CreateMaterialJson(0.8f), this.TestContext.CancellationToken).ConfigureAwait(false);
        }

        var after = (await reader.ReadAsync(project.Project, [uri], this.TestContext.CancellationToken).ConfigureAwait(false)).Single();
        _ = after.Freshness.Should().Be(AssetCookFreshness.OutOfDate);
        _ = after.HasPublishedOutput.Should().BeTrue();
        _ = after.HasVerifiedOutput.Should().Be(!damageOutput);
    }

    /// <summary>A source saved while native identity is being checked cannot receive a stale Current result.</summary>
    /// <returns>The asynchronous concurrent-input regression.</returns>
    [TestMethod]
    public async Task SourceChangedDuringInspectionCannotBeCurrent()
    {
        using var project = new StatusProject();
        await project.PublishAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        var native = new Mock<INativeCompatibilityService>(MockBehavior.Strict);
        _ = native.Setup(service => service.VerifyAsync(It.IsAny<Guid>(), It.IsAny<CancellationToken>()))
            .Returns(async (Guid operation, CancellationToken token) =>
            {
                await File.WriteAllTextAsync(project.SourcePath, StatusProject.CreateMaterialJson(0.8f), token).ConfigureAwait(false);
                return await project.VerifyNativeAsync(operation, token).ConfigureAwait(false);
            });
        var status = (await project.CreateReader(native.Object).ReadAsync(project.Project, [StatusProject.SourceUri], this.TestContext.CancellationToken).ConfigureAwait(false)).Single();
        _ = status.Freshness.Should().Be(AssetCookFreshness.Unknown);
        _ = status.HasVerifiedOutput.Should().BeTrue();
        _ = status.Diagnostics.Should().Contain(issue => issue.Code == "asset_status.input_changed");
    }

    /// <summary>An unavailable native producer does not erase prior output or pretend it is current.</summary>
    /// <returns>The asynchronous native-unavailability regression.</returns>
    [TestMethod]
    public async Task UnavailableProducerRetainsPriorOutputFacts()
    {
        using var project = new StatusProject();
        await project.PublishAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        await File.WriteAllTextAsync(Path.Combine(project.Root, "producer.bin"), "changed producer", this.TestContext.CancellationToken).ConfigureAwait(false);
        var status = await project.ReadAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = status.Freshness.Should().Be(AssetCookFreshness.Unknown);
        _ = status.HasPublishedOutput.Should().BeTrue();
        _ = status.HasVerifiedOutput.Should().BeTrue();
        _ = status.Diagnostics.Should().NotBeEmpty();
    }

    /// <summary>A different valid native build requires a recook without invalidating the prior bytes.</summary>
    /// <returns>The asynchronous producer-identity regression.</returns>
    [TestMethod]
    public async Task ChangedProducerRequiresCooking()
    {
        using var project = new StatusProject();
        await project.PublishAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        var producer = Path.Combine(project.Root, "producer.bin");
        await File.WriteAllTextAsync(producer, "new producer", this.TestContext.CancellationToken).ConfigureAwait(false);
        using var native = new Oxygen.Testing.TemporaryNativeArtifacts([new NativeArtifactLocation("producer", producer)]);
        var status = (await project.CreateReader(native).ReadAsync(project.Project, [StatusProject.SourceUri], this.TestContext.CancellationToken).ConfigureAwait(false)).Single();
        _ = status.Freshness.Should().Be(AssetCookFreshness.OutOfDate);
        _ = status.HasVerifiedOutput.Should().BeTrue();
        _ = status.Diagnostics.Should().BeEmpty();
    }

    /// <summary>A deleted source retains an unsaved owner's identity and prior output facts.</summary>
    /// <returns>The asynchronous missing-source regression.</returns>
    [TestMethod]
    public async Task MissingSourcePreservesDirtyOwnerAndPriorOutput()
    {
        using var project = new StatusProject();
        await project.PublishAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        using var owner = project.Documents.Register(project.SourcePath, _ => Task.FromResult<Snapshots.CookDocumentReadLease?>(
            new(new(Guid.NewGuid(), project.SourcePath, "Material", 2, 1, IsDirty: true, new string('A', 64)), static () => { })));
        File.Delete(project.SourcePath);
        var status = await project.ReadAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = status.Freshness.Should().Be(AssetCookFreshness.MissingSource);
        _ = status.HasUnsavedChanges.Should().BeTrue();
        _ = status.HasVerifiedOutput.Should().BeTrue();
    }

    private sealed partial class StatusProject
    {
        public Task<NativeCompatibilityResult> VerifyNativeAsync(Guid operation, CancellationToken token) => this.native.VerifyAsync(operation, token);
    }
}
