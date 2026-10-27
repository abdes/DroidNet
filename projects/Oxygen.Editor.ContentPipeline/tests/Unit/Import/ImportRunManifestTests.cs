// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json;
using AwesomeAssertions;
using Oxygen.Editor.Models;

namespace Oxygen.Editor.ContentPipeline.Unit.Tests.Import;

/// <summary>
/// An import's execution settings reach the native manifest as its top-level keys; what the run leaves unset is left
/// out, so the ImportTool takes its own default (model editor LLD section 7, Import run).
/// </summary>
[TestClass]
[System.Diagnostics.CodeAnalysis.SuppressMessage("Maintainability", "CA1515:Consider making public types internal", Justification = "MSTest discovers public test classes with the repository discovery configuration.")]
public sealed class ImportRunManifestTests
{
    private static readonly ContentImportManifest Manifest = new(1, @"C:\Cooked", new ContentImportLayout("/Content"), []);

    [TestMethod]
    public void WithRun_WritesTheSetValuesAtTheTopOfTheManifest()
    {
        var run = new ImportRunSettings { ThreadPoolSize = 6 }.WithPipeline("mesh_build", new(Workers: 3, QueueCapacity: null));

        using var document = JsonDocument.Parse(JsonSerializer.Serialize(Manifest.WithRun(run)));
        var root = document.RootElement;

        _ = root.GetProperty("thread_pool_size").GetInt32().Should().Be(6);
        _ = root.TryGetProperty("max_in_flight_jobs", out _).Should().BeFalse();
        _ = root.GetProperty("concurrency").EnumerateObject().Select(static pipeline => pipeline.Name).Should().Equal("mesh_build");
        _ = root.GetProperty("concurrency").GetProperty("mesh_build").GetProperty("workers").GetInt32().Should().Be(3);
        _ = root.GetProperty("concurrency").GetProperty("mesh_build").TryGetProperty("queue_capacity", out _).Should().BeFalse();
    }

    [TestMethod]
    public void WithRun_OfNoSettingsWritesNoExecutionKeys()
    {
        using var document = JsonDocument.Parse(JsonSerializer.Serialize(Manifest.WithRun(ImportRunSettings.None)));
        var root = document.RootElement;

        _ = root.TryGetProperty("thread_pool_size", out _).Should().BeFalse();
        _ = root.TryGetProperty("concurrency", out _).Should().BeFalse();
    }
}
