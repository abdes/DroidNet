// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Security.Cryptography;
using System.Text.Json.Nodes;
using AwesomeAssertions;
using DroidNet.Storage;
using DroidNet.Storage.Native;
using Oxygen.Editor.ContentPipeline.Import;
using Testably.Abstractions;

namespace Oxygen.Editor.ContentPipeline.Tests;

/// <summary>Verifies portable native settings and non-overwriting source configuration.</summary>
[TestClass]
[System.Diagnostics.CodeAnalysis.SuppressMessage("Maintainability", "CA1515:Consider making public types internal", Justification = "MSTest discovers public test classes with the repository discovery configuration.")]
public sealed class NativeSceneImportSettingsTests
{
    /// <summary>Gets or sets cancellation for the current test.</summary>
    public TestContext TestContext { get; set; } = null!;

    /// <summary>All applied native options survive sidecar serialization.</summary>
    [TestMethod]
    public void SettingsRoundTripIncludesExactNativeOptions()
    {
        var settings = CreateSettings();
        var parsed = NativeSceneImportSettings.Parse(settings.ToBytes());
        _ = parsed.Importer.Should().Be(NativeSceneImportSettings.ImporterIdentity);
        _ = parsed.OutputPrefix.Should().Be("/Content/Models/Model/");
        _ = parsed.Files.Should().Equal("model.gltf");
        _ = parsed.ContentPolicy.Should().Be("static-scalar");
        _ = parsed.UnitPolicy.Should().Be("normalize");
        _ = parsed.BakeTransforms.Should().BeFalse();
        _ = parsed.NormalsPolicy.Should().Be("generate");
        _ = parsed.TangentsPolicy.Should().Be("preserve");
    }

    /// <summary>Legacy identities and unsupported policies are never silently interpreted as native settings.</summary>
    /// <param name="field">The incompatible field.</param>
    [TestMethod]
    [DataRow("SchemaVersion")]
    [DataRow("Importer")]
    [DataRow("UnitPolicy")]
    public void InvalidNativeSettingsAreRejected(string field)
    {
        var json = JsonNode.Parse(CreateSettings().ToBytes())!;
        json[field] = string.Equals(field, "SchemaVersion", StringComparison.Ordinal) ? JsonValue.Create(1) : JsonValue.Create("legacy");
        Action parse = () => _ = NativeSceneImportSettings.Parse(System.Text.Encoding.UTF8.GetBytes(json.ToJsonString()));
        _ = parse.Should().Throw<InvalidDataException>();
    }

    /// <summary>Unsafe output and source paths cannot escape their retained namespace.</summary>
    /// <param name="path">The invalid relative path.</param>
    [TestMethod]
    [DataRow("../escape")]
    [DataRow("C:/outside")]
    [DataRow("Model/../Elsewhere")]
    public void EscapingPathsAreRejected(string path)
    {
        Action write = () => _ = (CreateSettings() with { OutputDirectory = path }).ToBytes();
        _ = write.Should().Throw<InvalidDataException>();
    }

    /// <summary>Creating native settings cannot overwrite an existing sidecar or its identity records.</summary>
    /// <returns>The asynchronous creation-conflict test.</returns>
    [TestMethod]
    public async Task ExistingSidecarIsPreserved()
    {
        var directory = Directory.CreateTempSubdirectory("OxygenNativeSourceSettings-");
        try
        {
            var settings = CreateSettings();
            var path = settings.ResolveFile(directory.FullName, settings.PrimaryRelativePath) + NativeSceneImportSettings.SidecarSuffix;
            _ = Directory.CreateDirectory(Path.GetDirectoryName(path)!);
            await File.WriteAllTextAsync(path, "existing identity records", this.TestContext.CancellationToken).ConfigureAwait(false);
            var files = new NativeAtomicFileStore(new RealFileSystem());
            Func<Task> create = () => settings.SaveNewAsync(directory.FullName, files, this.TestContext.CancellationToken);
            _ = await create.Should().ThrowAsync<StorageWriteConflictException>().ConfigureAwait(false);
            _ = (await File.ReadAllTextAsync(path, this.TestContext.CancellationToken).ConfigureAwait(false)).Should().Be("existing identity records");
        }
        finally
        {
            directory.Delete(recursive: true);
        }
    }

    private static NativeSceneImportSettings CreateSettings()
        => NativeSceneImportSettings.Create(new("Content/SourceMedia/DCC/Model", "model.gltf", [new("model.gltf", Convert.ToHexString(SHA256.HashData("source"u8)))]), "Content", "Model", "Models/Model");
}
