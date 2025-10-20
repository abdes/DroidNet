// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using System.Text.Json;
using System.Text.Json.Serialization;
using DroidNet.Config.Sources;
using DroidNet.Config.Tests.Helpers;
using FluentAssertions;

namespace DroidNet.Config.Tests.Sources;

[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory("JSON Settings Source")]
public class JsonSettingsSourceTests : SettingsTestBase
{
    public TestContext TestContext { get; set; }

    [TestMethod]
    public async Task LoadAsync_ReturnsEmptyPayload_WhenFileMissing()
    {
        var filePath = this.FileSystem.Path.Combine(this.FileSystem.Path.GetTempPath(), "missing.json");
        using var source = new JsonSettingsSource("test", filePath, this.FileSystem, watch: false, crypto: null, this.LoggerFactory);

        var result = await source.LoadAsync(cancellationToken: this.TestContext.CancellationToken).ConfigureAwait(true);

        _ = result.IsSuccess.Should().BeTrue();
        _ = result.Value.Sections.Should().BeEmpty();
        _ = result.Value.Metadata.Should().BeNull();
    }

    [TestMethod]
    public async Task LoadAsync_ReturnsFailure_WhenMetadataMissing()
    {
        var jsonWithoutMetadata = JsonSerializer.Serialize(new
        {
            Sample = new { Value = 42 },
        });
        var filePath = this.CreateTempSettingsFile("missing-metadata.json", jsonWithoutMetadata);
        using var source = new JsonSettingsSource("test", filePath, this.FileSystem, watch: false, crypto: null, this.LoggerFactory);

        var result = await source.LoadAsync(cancellationToken: this.TestContext.CancellationToken).ConfigureAwait(true);

        _ = result.IsSuccess.Should().BeFalse();
        _ = result.Error.Should().BeOfType<SettingsPersistenceException>();
        _ = result.Error!.InnerException.Should().BeOfType<JsonException>();
    }

    [TestMethod]
    public async Task LoadAsync_ReturnsFailure_WhenFileEmpty()
    {
        var filePath = this.CreateTempSettingsFile("empty.json", string.Empty);
        using var source = new JsonSettingsSource("test", filePath, this.FileSystem, watch: false, crypto: null, this.LoggerFactory);

        var result = await source.LoadAsync(cancellationToken: this.TestContext.CancellationToken).ConfigureAwait(true);

        _ = result.IsSuccess.Should().BeFalse();
        _ = result.Error.Should().BeOfType<SettingsPersistenceException>();
    }

    [TestMethod]
    public async Task LoadAsync_ReturnsFailure_WhenJsonMalformed()
    {
        const string malformedJson = "{ \"metadata\": \"incomplete";
        var filePath = this.CreateTempSettingsFile("malformed.json", malformedJson);
        using var source = new JsonSettingsSource("test", filePath, this.FileSystem, watch: false, crypto: null, this.LoggerFactory);

        var result = await source.LoadAsync(cancellationToken: this.TestContext.CancellationToken).ConfigureAwait(true);

        _ = result.IsSuccess.Should().BeFalse();
        _ = result.Error.Should().BeOfType<SettingsPersistenceException>();
        _ = result.Error!.InnerException.Should().BeAssignableTo<JsonException>();
        _ = result.Error.Message.Should().Contain("Failed to deserialize JSON content.");
    }

    [TestMethod]
    public async Task SaveAsync_WritesMetadataAndSectionsInCamelCase()
    {
        var filePath = this.FileSystem.Path.Combine(this.FileSystem.Path.GetTempPath(), "structure.json");
        using var source = new JsonSettingsSource("test", filePath, this.FileSystem, watch: false, crypto: null, this.LoggerFactory);

        var metadata = new SettingsMetadata { Version = "1.2.3", SchemaVersion = "20251020" };
        var sections = new Dictionary<string, object>(StringComparer.Ordinal)
        {
            [nameof(TestSettings)] = new TestSettings { Name = "Json Source", Value = 7 },
        };

        var result = await source.SaveAsync(sections, metadata, this.TestContext.CancellationToken).ConfigureAwait(true);

        _ = result.IsSuccess.Should().BeTrue();

        var content = await this.FileSystem.File.ReadAllTextAsync(filePath, default).ConfigureAwait(true);
        using var document = JsonDocument.Parse(content);
        var root = document.RootElement;

        _ = root.TryGetProperty("metadata", out var metadataElement).Should().BeTrue();
        _ = metadataElement.TryGetProperty("Version", out var version).Should().BeTrue();
        _ = version.GetString().Should().Be("1.2.3");
        _ = metadataElement.TryGetProperty("SchemaVersion", out var schemaVersion).Should().BeTrue();
        _ = schemaVersion.GetString().Should().Be("20251020");

        _ = root.TryGetProperty(nameof(TestSettings), out var sectionElement).Should().BeTrue();
        _ = sectionElement.TryGetProperty("name", out var name).Should().BeTrue();
        _ = name.GetString().Should().Be("Json Source");
    }

    [TestMethod]
    public async Task SaveAsync_CreatesMissingDirectory()
    {
        var directory = this.FileSystem.Path.Combine(this.FileSystem.Path.GetTempPath(), "json", "nested");
        var filePath = this.FileSystem.Path.Combine(directory, "settings.json");
        using var source = new JsonSettingsSource("test", filePath, this.FileSystem, watch: false, crypto: null, this.LoggerFactory);

        var metadata = new SettingsMetadata { Version = "1.0", SchemaVersion = "20251020" };
        var sections = new Dictionary<string, object>(StringComparer.Ordinal) { ["Section"] = new TestSettings { Name = "Created", Value = 1 } };

        var result = await source.SaveAsync(sections, metadata, this.TestContext.CancellationToken).ConfigureAwait(true);

        _ = result.IsSuccess.Should().BeTrue();
        _ = this.FileSystem.Directory.Exists(directory).Should().BeTrue();
        _ = this.FileSystem.File.Exists(filePath).Should().BeTrue();
    }

    [TestMethod]
    public async Task SaveAsync_MergesExistingSections()
    {
        var initialSections = new Dictionary<string, object>(StringComparer.Ordinal)
        {
            ["Section1"] = new TestSettings { Name = "First", Value = 1 },
            ["Section2"] = new AlternativeTestSettings { Theme = "Light", FontSize = 12 },
        };
        var existingPath = this.CreateMultiSectionSettingsFile("merge.json", initialSections);
        using var source = new JsonSettingsSource("test", existingPath, this.FileSystem, watch: false, crypto: null, this.LoggerFactory);

        var metadata = new SettingsMetadata { Version = "2.0", SchemaVersion = "20251020" };
        var update = new Dictionary<string, object>(StringComparer.Ordinal)
        {
            ["Section3"] = new TestSettings { Name = "Third", Value = 3 },
        };

        var result = await source.SaveAsync(update, metadata, this.TestContext.CancellationToken).ConfigureAwait(true);

        _ = result.IsSuccess.Should().BeTrue();

        var content = await this.FileSystem.File.ReadAllTextAsync(existingPath, default).ConfigureAwait(true);
        using var document = JsonDocument.Parse(content);
        var root = document.RootElement;
        _ = root.TryGetProperty("Section1", out _).Should().BeTrue();
        _ = root.TryGetProperty("Section2", out _).Should().BeTrue();
        _ = root.TryGetProperty("Section3", out var section3).Should().BeTrue();
        _ = section3.TryGetProperty("name", out var name).Should().BeTrue();
        _ = name.GetString().Should().Be("Third");
    }

    [TestMethod]
    public async Task SaveAsync_ReturnsFailure_WhenExistingContentInvalid()
    {
        var invalidExistingJson = JsonSerializer.Serialize(new { Section = new { Value = 5 } });
        var filePath = this.CreateTempSettingsFile("invalid-existing.json", invalidExistingJson);
        using var source = new JsonSettingsSource("test", filePath, this.FileSystem, watch: false, crypto: null, this.LoggerFactory);

        var metadata = new SettingsMetadata { Version = "1.0", SchemaVersion = "20251020" };
        var sections = new Dictionary<string, object>(StringComparer.Ordinal) { ["Section"] = new TestSettings { Name = "New", Value = 10 } };

        var result = await source.SaveAsync(sections, metadata, this.TestContext.CancellationToken).ConfigureAwait(true);

        _ = result.IsSuccess.Should().BeFalse();
        _ = result.Error.Should().BeOfType<SettingsPersistenceException>();
    }

    [TestMethod]
    public async Task SaveAsync_ReturnsFailure_WhenSerializationThrowsJsonException()
    {
        var filePath = this.FileSystem.Path.Combine(this.FileSystem.Path.GetTempPath(), "serialize-fail.json");
        using var source = new JsonSettingsSource("test", filePath, this.FileSystem, watch: false, crypto: null, this.LoggerFactory);

        var metadata = new SettingsMetadata { Version = "1.0", SchemaVersion = "20251020" };
        var sections = new Dictionary<string, object>(StringComparer.Ordinal)
        {
            ["ThrowingSection"] = new ThrowingSection { Value = 123 },
        };

        var result = await source.SaveAsync(sections, metadata, this.TestContext.CancellationToken).ConfigureAwait(true);

        _ = result.IsSuccess.Should().BeFalse();
        _ = result.Error.Should().BeOfType<SettingsPersistenceException>();
        _ = result.Error!.InnerException.Should().BeAssignableTo<JsonException>();
        _ = result.Error.Message.Should().Contain("Failed to serialize settings to JSON.");
    }

    [TestMethod]
    public async Task ValidateAsync_ReturnsSuccess_ForSerializableSections()
    {
        var filePath = this.FileSystem.Path.Combine(this.FileSystem.Path.GetTempPath(), "validate.json");
        using var source = new JsonSettingsSource("test", filePath, this.FileSystem, watch: false, crypto: null, this.LoggerFactory);

        var sections = new Dictionary<string, object>(StringComparer.Ordinal)
        {
            ["Section"] = new TestSettings { Name = "Valid", Value = 99 },
        };

        var result = await source.ValidateAsync(sections, this.TestContext.CancellationToken).ConfigureAwait(true);

        _ = result.IsSuccess.Should().BeTrue();
        _ = result.Value.SectionsValidated.Should().Be(1);
        _ = result.Value.Message.Should().Contain("Validated");
    }

    [TestMethod]
    public async Task ValidateAsync_ReturnsFailure_ForUnsupportedSectionType()
    {
        var filePath = this.FileSystem.Path.Combine(this.FileSystem.Path.GetTempPath(), "validate-fail.json");
        using var source = new JsonSettingsSource("test", filePath, this.FileSystem, watch: false, crypto: null, this.LoggerFactory);

        var sections = new Dictionary<string, object>(StringComparer.Ordinal)
        {
            ["Section"] = new Action(() => { }),
        };

        var result = await source.ValidateAsync(sections, this.TestContext.CancellationToken).ConfigureAwait(true);

        _ = result.IsSuccess.Should().BeFalse();
        _ = result.Error.Should().BeOfType<SettingsPersistenceException>();
        _ = result.Error!.InnerException.Should().BeOfType<NotSupportedException>();
    }

    private sealed class ThrowingSection
    {
        [JsonConverter(typeof(JsonWriteThrowsConverter))]
        public int Value { get; set; }
    }

    [System.Diagnostics.CodeAnalysis.SuppressMessage("Performance", "CA1812:Avoid uninstantiated internal classes", Justification = "Instantiated by test runtime via reflection")]
    private sealed class JsonWriteThrowsConverter : JsonConverter<int>
    {
        public override int Read(ref Utf8JsonReader reader, Type typeToConvert, JsonSerializerOptions options) => reader.GetInt32();

        public override void Write(Utf8JsonWriter writer, int value, JsonSerializerOptions options) => throw new JsonException("Serialization failed for test coverage.");
    }
}
