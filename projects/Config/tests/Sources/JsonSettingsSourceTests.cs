// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using System.IO.Abstractions;
using System.Text.Json;
using System.Text.Json.Serialization;
using DroidNet.Config.Sources;
using DroidNet.Config.Tests.Helpers;
using AwesomeAssertions;
using Moq;

namespace DroidNet.Config.Tests.Sources;

[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory("JSON Settings Source")]
public class JsonSettingsSourceTests : SettingsTestBase
{
    [TestMethod]
    public async Task LoadAsync_ReturnsEmptyPayload_WhenFileMissing()
    {
        var filePath = this.FileSystem.Path.Combine(this.FileSystem.Path.GetTempPath(), "missing.json");
        using var source = new JsonSettingsSource("test", filePath, this.FileSystem, watch: false, crypto: null, this.LoggerFactory);

        var result = await source.LoadAsync(cancellationToken: this.TestContext.CancellationToken).ConfigureAwait(true);

        _ = result.IsSuccess.Should().BeTrue();
        _ = result.Value.Sections.Should().BeEmpty();
        _ = result.Value.SourceMetadata.Should().BeNull();
    }

    [TestMethod]
    public async Task LoadAsync_ReturnsSuccess_WhenMetadataMissing()
    {
        var jsonWithoutMetadata = JsonSerializer.Serialize(new
        {
            Sample = new { Value = 42 },
        });
        var filePath = this.CreateTempSettingsFile("missing-metadata.json", jsonWithoutMetadata);
        using var source = new JsonSettingsSource("test", filePath, this.FileSystem, watch: false, crypto: null, this.LoggerFactory);

        var result = await source.LoadAsync(cancellationToken: this.TestContext.CancellationToken).ConfigureAwait(true);

        _ = result.IsSuccess.Should().BeTrue();
        _ = result.Value.SourceMetadata.Should().BeNull("no $meta key at root level");
        _ = result.Value.Sections.Should().ContainKey("Sample");
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

        var sourceMetadata = new SettingsSourceMetadata
        {
            WrittenAt = DateTimeOffset.UtcNow,
            WrittenBy = "Test",
        };
        var sectionMetadata = new Dictionary<string, SettingsSectionMetadata>(StringComparer.Ordinal)
        {
            [nameof(TestSettings)] = new SettingsSectionMetadata { SchemaVersion = "20251020", Service = "TestService" },
        };
        var sections = new Dictionary<string, object>(StringComparer.Ordinal)
        {
            [nameof(TestSettings)] = new TestSettings { Name = "Json Source", Value = 7 },
        };

        var result = await source.SaveAsync(sections, sectionMetadata, sourceMetadata, this.TestContext.CancellationToken).ConfigureAwait(true);

        _ = result.IsSuccess.Should().BeTrue();

        var content = await this.FileSystem.File.ReadAllTextAsync(filePath, default).ConfigureAwait(true);
        using var document = JsonDocument.Parse(content);
        var root = document.RootElement;

        // Check source metadata
        _ = root.TryGetProperty("$meta", out var sourceMetaElement).Should().BeTrue();
        _ = sourceMetaElement.TryGetProperty("writtenBy", out var writtenBy).Should().BeTrue();
        _ = writtenBy.GetString().Should().Be("Test");

        // Check section structure
        _ = root.TryGetProperty(nameof(TestSettings), out var sectionElement).Should().BeTrue();
        _ = sectionElement.TryGetProperty("$meta", out var sectionMetaElement).Should().BeTrue();
        _ = sectionMetaElement.TryGetProperty("schemaVersion", out var schemaVersion).Should().BeTrue();
        _ = schemaVersion.GetString().Should().Be("20251020");

        // Check section data
        _ = sectionElement.TryGetProperty("name", out var name).Should().BeTrue();
        _ = name.GetString().Should().Be("Json Source");
    }

    [TestMethod]
    public async Task SaveAsync_CreatesMissingDirectory()
    {
        var directory = this.FileSystem.Path.Combine(this.FileSystem.Path.GetTempPath(), "json", "nested");
        var filePath = this.FileSystem.Path.Combine(directory, "settings.json");
        using var source = new JsonSettingsSource("test", filePath, this.FileSystem, watch: false, crypto: null, this.LoggerFactory);

        var sourceMetadata = new SettingsSourceMetadata { WrittenAt = DateTimeOffset.UtcNow, WrittenBy = "Test" };
        var sectionMetadata = new Dictionary<string, SettingsSectionMetadata>(StringComparer.Ordinal)
        {
            ["Section"] = new SettingsSectionMetadata { SchemaVersion = "20251020", Service = "TestService" },
        };
        var sections = new Dictionary<string, object>(StringComparer.Ordinal) { ["Section"] = new TestSettings { Name = "Created", Value = 1 } };

        var result = await source.SaveAsync(sections, sectionMetadata, sourceMetadata, this.TestContext.CancellationToken).ConfigureAwait(true);

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

        var sourceMetadata = new SettingsSourceMetadata { WrittenAt = DateTimeOffset.UtcNow, WrittenBy = "Test" };
        var sectionMetadata = new Dictionary<string, SettingsSectionMetadata>(StringComparer.Ordinal)
        {
            ["Section3"] = new SettingsSectionMetadata { SchemaVersion = "20251020", Service = "TestService" },
        };
        var update = new Dictionary<string, object>(StringComparer.Ordinal)
        {
            ["Section3"] = new TestSettings { Name = "Third", Value = 3 },
        };

        var result = await source.SaveAsync(update, sectionMetadata, sourceMetadata, this.TestContext.CancellationToken).ConfigureAwait(true);

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
    public async Task SaveAsync_Succeeds_WhenExistingContentWithoutMetadata()
    {
        var invalidExistingJson = JsonSerializer.Serialize(new { Section = new { Value = 5 } });
        var filePath = this.CreateTempSettingsFile("existing-without-metadata.json", invalidExistingJson);
        using var source = new JsonSettingsSource("test", filePath, this.FileSystem, watch: false, crypto: null, this.LoggerFactory);

        var sourceMetadata = new SettingsSourceMetadata { WrittenAt = DateTimeOffset.UtcNow, WrittenBy = "Test" };
        var sectionMetadata = new Dictionary<string, SettingsSectionMetadata>(StringComparer.Ordinal)
        {
            ["Section"] = new SettingsSectionMetadata { SchemaVersion = "20251020", Service = "TestService" },
        };
        var sections = new Dictionary<string, object>(StringComparer.Ordinal) { ["Section"] = new TestSettings { Name = "New", Value = 10 } };

        var result = await source.SaveAsync(sections, sectionMetadata, sourceMetadata, this.TestContext.CancellationToken).ConfigureAwait(true);

        _ = result.IsSuccess.Should().BeTrue("existing content without $meta can be merged");
    }

    [TestMethod]
    public async Task SaveAsync_ReturnsFailure_WhenSerializationThrowsJsonException()
    {
        var filePath = this.FileSystem.Path.Combine(this.FileSystem.Path.GetTempPath(), "serialize-fail.json");
        using var source = new JsonSettingsSource("test", filePath, this.FileSystem, watch: false, crypto: null, this.LoggerFactory);

        var sourceMetadata = new SettingsSourceMetadata { WrittenAt = DateTimeOffset.UtcNow, WrittenBy = "Test" };
        var sectionMetadata = new Dictionary<string, SettingsSectionMetadata>(StringComparer.Ordinal)
        {
            ["ThrowingSection"] = new SettingsSectionMetadata { SchemaVersion = "20251020", Service = "TestService" },
        };
        var sections = new Dictionary<string, object>(StringComparer.Ordinal)
        {
            ["ThrowingSection"] = new ThrowingSection { Value = 123 },
        };

        var result = await source.SaveAsync(sections, sectionMetadata, sourceMetadata, this.TestContext.CancellationToken).ConfigureAwait(true);

        _ = result.IsSuccess.Should().BeFalse();
        _ = result.Error.Should().BeOfType<SettingsPersistenceException>();
        _ = result.Error!.InnerException.Should().BeAssignableTo<JsonException>();
        _ = result.Error.Message.Should().Contain("Failed to serialize settings to JSON.");
    }

    [TestMethod]
    public async Task SaveAsync_ReturnsFailure_WhenWriteFails()
    {
        var filePath = this.FileSystem.Path.Combine(this.FileSystem.Path.GetTempPath(), "write-fail.json");

        // Create a mock IFileSystem where WriteAllBytesAsync throws an IOException
        var mockFs = new Mock<IFileSystem>();
        var mockFile = new Mock<IFile>();
        _ = mockFile.Setup(f => f.WriteAllBytesAsync(It.IsAny<string>(), It.IsAny<byte[]>(), It.IsAny<CancellationToken>()))
            .ThrowsAsync(new IOException("simulated write failure"));

        _ = mockFs.SetupGet(m => m.File).Returns(mockFile.Object);
        _ = mockFs.SetupGet(m => m.Path).Returns(this.FileSystem.Path);
        _ = mockFs.SetupGet(m => m.Directory).Returns(this.FileSystem.Directory);
        _ = mockFs.SetupGet(m => m.FileInfo).Returns(this.FileSystem.FileInfo);

        using var source = new JsonSettingsSource("test", filePath, mockFs.Object, watch: false, crypto: null, this.LoggerFactory);

        var sourceMetadata = new SettingsSourceMetadata { WrittenAt = DateTimeOffset.UtcNow, WrittenBy = "Test" };
        var sectionMetadata = new Dictionary<string, SettingsSectionMetadata>(StringComparer.Ordinal)
        {
            ["Section"] = new SettingsSectionMetadata { SchemaVersion = "20251020", Service = "TestService" },
        };
        var sections = new Dictionary<string, object>(StringComparer.Ordinal) { ["Section"] = new TestSettings { Name = "New", Value = 10 } };

        var result = await source.SaveAsync(sections, sectionMetadata, sourceMetadata, this.TestContext.CancellationToken).ConfigureAwait(true);

        _ = result.IsSuccess.Should().BeFalse();
        _ = result.Error.Should().BeOfType<SettingsPersistenceException>();
        _ = result.Error!.InnerException.Should().BeAssignableTo<IOException>();
    }

    [TestMethod]
    public async Task SaveAsync_ReturnsFailure_WhenExistingReadFails()
    {
        var filePath = this.FileSystem.Path.Combine(this.FileSystem.Path.GetTempPath(), "merge-read-fail.json");

        // Mock IFileSystem where File.Exists returns true and ReadAllBytesAsync throws
        var mockFs = new Mock<IFileSystem>();
        var mockFile = new Mock<IFile>();

        _ = mockFile.Setup(f => f.Exists(It.IsAny<string>())).Returns(value: true);
        _ = mockFile.Setup(f => f.ReadAllBytesAsync(It.IsAny<string>(), It.IsAny<CancellationToken>()))
            .ThrowsAsync(new IOException("simulated read failure"));

        _ = mockFs.SetupGet(m => m.File).Returns(mockFile.Object);
        _ = mockFs.SetupGet(m => m.Path).Returns(this.FileSystem.Path);
        _ = mockFs.SetupGet(m => m.Directory).Returns(this.FileSystem.Directory);
        _ = mockFs.SetupGet(m => m.FileInfo).Returns(this.FileSystem.FileInfo);

        using var source = new JsonSettingsSource("test", filePath, mockFs.Object, watch: false, crypto: null, this.LoggerFactory);

        var sourceMetadata = new SettingsSourceMetadata { WrittenAt = DateTimeOffset.UtcNow, WrittenBy = "Test" };
        var sectionMetadata = new Dictionary<string, SettingsSectionMetadata>(StringComparer.Ordinal)
        {
            ["Section"] = new SettingsSectionMetadata { SchemaVersion = "20251020", Service = "TestService" },
        };
        var sections = new Dictionary<string, object>(StringComparer.Ordinal) { ["Section"] = new TestSettings { Name = "New", Value = 10 } };

        var result = await source.SaveAsync(sections, sectionMetadata, sourceMetadata, this.TestContext.CancellationToken).ConfigureAwait(true);

        _ = result.IsSuccess.Should().BeFalse();
        _ = result.Error.Should().BeOfType<SettingsPersistenceException>();
        _ = result.Error!.InnerException.Should().BeAssignableTo<IOException>();
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

    [TestMethod]
    public async Task ValidateAsync_ReturnsFailure_WhenJsonSerializationThrows()
    {
        var filePath = this.FileSystem.Path.Combine(this.FileSystem.Path.GetTempPath(), "validate-serialize-exception.json");
        using var source = new JsonSettingsSource("test", filePath, this.FileSystem, watch: false, crypto: null, this.LoggerFactory);

        var sections = new Dictionary<string, object>(StringComparer.Ordinal)
        {
            ["Section"] = new ThrowingSection { Value = 123 },
        };

        var result = await source.ValidateAsync(sections, this.TestContext.CancellationToken).ConfigureAwait(true);

        _ = result.IsSuccess.Should().BeFalse();
        _ = result.Error.Should().BeOfType<SettingsPersistenceException>();
        _ = result.Error!.InnerException.Should().BeAssignableTo<JsonException>();
        _ = result.Error.Message.Should().Contain("Content could not be serialized to JSON.");
    }

    [TestMethod]
    public void Dispose_WhenCalledTwice_DoesNotThrow()
    {
        var path = this.FileSystem.Path.Combine(this.FileSystem.Path.GetTempPath(), "dispose-twice.json");
        var source = new JsonSettingsSource("test", path, this.FileSystem, watch: false, crypto: null, this.LoggerFactory);

        // First dispose should run normal disposal path
        source.Dispose();

        // Second dispose should hit the early-return branch and not throw
        Action act = source.Dispose;
        _ = act.Should().NotThrow();
    }

    [TestMethod]
    public async Task SaveAsync_WritesSection_FromDictionary()
    {
        var filePath = this.FileSystem.Path.Combine(this.FileSystem.Path.GetTempPath(), "dict-section.json");
        using var source = new JsonSettingsSource("test", filePath, this.FileSystem, watch: false, crypto: null, this.LoggerFactory);

        var sourceMetadata = new SettingsSourceMetadata { WrittenAt = DateTimeOffset.UtcNow, WrittenBy = "Test" };
        var sectionMetadata = new Dictionary<string, SettingsSectionMetadata>(StringComparer.Ordinal)
        {
            ["DictSection"] = new SettingsSectionMetadata { SchemaVersion = "20251020", Service = "TestService" },
        };

        var sectionData = new Dictionary<string, object>(StringComparer.Ordinal)
        {
            ["name"] = "FromDict",
            ["value"] = 42,
        };

        var sections = new Dictionary<string, object>(StringComparer.Ordinal) { ["DictSection"] = sectionData };

        var result = await source.SaveAsync(sections, sectionMetadata, sourceMetadata, this.TestContext.CancellationToken).ConfigureAwait(true);

        _ = result.IsSuccess.Should().BeTrue();

        var content = await this.FileSystem.File.ReadAllTextAsync(filePath, default).ConfigureAwait(true);
        using var document = JsonDocument.Parse(content);
        var root = document.RootElement;

        _ = root.TryGetProperty("DictSection", out var sectionElement).Should().BeTrue();
        _ = sectionElement.TryGetProperty("name", out var name).Should().BeTrue();
        _ = name.GetString().Should().Be("FromDict");
        _ = sectionElement.TryGetProperty("value", out var value).Should().BeTrue();
        _ = value.GetInt32().Should().Be(42);
    }

    [TestMethod]
    public async Task LoadAsync_ReadsSectionMetadata_WhenPresent()
    {
        var sections = new Dictionary<string, object>(StringComparer.Ordinal)
        {
            ["SectionWithMeta"] = new TestSettings { Name = "HasMeta", Value = 5 },
        };

        var sectionMetadata = new Dictionary<string, SettingsSectionMetadata>(StringComparer.Ordinal)
        {
            ["SectionWithMeta"] = new SettingsSectionMetadata { SchemaVersion = "20251020", Service = "MetaService" },
        };

        var path = this.CreateMultiSectionSettingsFile("with-section-meta.json", sections, sourceMetadata: null, sectionMetadata: sectionMetadata);

        using var source = new JsonSettingsSource("test", path, this.FileSystem, watch: false, crypto: null, this.LoggerFactory);

        var result = await source.LoadAsync(cancellationToken: this.TestContext.CancellationToken).ConfigureAwait(true);

        _ = result.IsSuccess.Should().BeTrue();
        _ = result.Value.SectionMetadata.Should().ContainKey("SectionWithMeta");
        var meta = result.Value.SectionMetadata["SectionWithMeta"];
        _ = meta.SchemaVersion.Should().Be("20251020");
        _ = meta.Service.Should().Be("MetaService");
    }

    [TestMethod]
    public async Task LoadAsync_ReturnsFailure_WhenSectionIsNotObject()
    {
        var invalidJson = JsonSerializer.Serialize(new { BadSection = 123 });
        var filePath = this.CreateTempSettingsFile("section-not-object.json", invalidJson);
        using var source = new JsonSettingsSource("test", filePath, this.FileSystem, watch: false, crypto: null, this.LoggerFactory);

        var result = await source.LoadAsync(cancellationToken: this.TestContext.CancellationToken).ConfigureAwait(true);

        _ = result.IsSuccess.Should().BeFalse();
        _ = result.Error.Should().BeOfType<SettingsPersistenceException>();
        _ = result.Error!.InnerException.Should().BeAssignableTo<JsonException>();
        _ = result.Error.Message.Should().Contain("must be a JSON object");
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
