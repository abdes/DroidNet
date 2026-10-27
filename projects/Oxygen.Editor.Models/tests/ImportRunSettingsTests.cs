// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text;
using System.Text.Json;
using AwesomeAssertions;
using Oxygen.Editor.Models.Tests.TestSupport;

namespace Oxygen.Editor.Models.Tests;

/// <summary>
/// A run's execution settings are the import manifest's top-level keys; unset ones take the defaults the installed
/// schema states (model editor LLD section 7, Import run).
/// </summary>
[TestClass]
public sealed class ImportRunSettingsTests
{
    [TestMethod]
    public void Defaults_AreTheSchemasAndTheProcessorCount()
    {
        var concurrency = InstalledSchemas.ImportManifest()["properties"]!["concurrency"]!["properties"]!;

        var defaults = ImportRunSettings.Defaults();

        _ = defaults.ThreadPoolSize.Should().Be(Environment.ProcessorCount);
        _ = defaults.MaxInFlightJobs.Should().Be(Environment.ProcessorCount);
        _ = defaults.Concurrency.Keys.Should().BeEquivalentTo(ImportRunSettings.Pipelines);
        foreach (var name in ImportRunSettings.Pipelines)
        {
            _ = defaults.Concurrency[name].Workers.Should().Be(concurrency[name]!["properties"]!["workers"]!["default"]!.GetValue<int>());
            _ = defaults.Concurrency[name].QueueCapacity.Should().Be(concurrency[name]!["properties"]!["queue_capacity"]!["default"]!.GetValue<int>());
        }
    }

    [TestMethod]
    public void WriteTo_WritesOnlyWhatIsSet_AndReadsItBack()
    {
        var run = new ImportRunSettings { ThreadPoolSize = 8 }
            .WithPipeline("texture", new(Workers: 4, QueueCapacity: null))
            .WithPipeline("scene", new(Workers: null, QueueCapacity: null));

        var json = Write(run);
        using var document = JsonDocument.Parse(json);
        var read = ImportRunSettings.Read(document.RootElement);

        _ = document.RootElement.TryGetProperty("max_in_flight_jobs", out _).Should().BeFalse();
        _ = document.RootElement.GetProperty("concurrency").EnumerateObject().Select(static pipeline => pipeline.Name).Should().Equal("texture");
        _ = document.RootElement.GetProperty("concurrency").GetProperty("texture").TryGetProperty("queue_capacity", out _).Should().BeFalse();
        _ = read.ThreadPoolSize.Should().Be(8);
        _ = read.Concurrency["texture"].Should().Be(new ImportPipelineSettings(4, QueueCapacity: null));
        _ = ImportRunSettings.None.IsEmpty.Should().BeTrue();
        _ = run.IsEmpty.Should().BeFalse();
    }

    [TestMethod]
    public void WithPipeline_RefusesAPipelineTheToolDoesNotHave()
    {
        var set = () => ImportRunSettings.None.WithPipeline("audio", new(1, 1));

        _ = set.Should().Throw<ArgumentException>();
    }

    private static string Write(ImportRunSettings run)
    {
        using var stream = new MemoryStream();
        using (var writer = new Utf8JsonWriter(stream))
        {
            writer.WriteStartObject();
            run.WriteTo(writer);
            writer.WriteEndObject();
        }

        return Encoding.UTF8.GetString(stream.ToArray());
    }
}
