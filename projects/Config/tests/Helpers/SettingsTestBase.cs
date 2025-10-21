// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using System.Text.Json;
using DryIoc;
using Microsoft.Extensions.Logging;
using Testably.Abstractions.Testing;

namespace DroidNet.Config.Tests.Helpers;

/// <summary>
/// Base class for tests that require a properly configured DI container with settings services.
/// </summary>
[ExcludeFromCodeCoverage]
public abstract class SettingsTestBase : IDisposable
{
    // Add a static readonly field for the options at the class level
    private static readonly JsonSerializerOptions JsonOptions = new()
    {
        WriteIndented = true,
    };

    private bool isDisposed;

    protected SettingsTestBase(bool withSettings = true)
    {
        this.FileSystem = new MockFileSystem();
        this.Container = new Container();
        this.LoggerFactory = new LoggerFactory();

        // Register common services
        this.Container.RegisterInstance(this.LoggerFactory);
        this.Container.RegisterInstance(this.FileSystem);

        // All tests will need a SettingsManager
        this.Container.Register<SettingsManager>(Reuse.Singleton);

        // Register default settings services if not disabled
        if (withSettings)
        {
            _ = this.Container
                .WithSettings<ITestSettings, TestSettingsService>()
                .WithSettings<IAlternativeTestSettings, AlternativeTestSettingsService>()
                .WithSettings<ITestSettingsWithValidation, TestSettingsServiceWithValidation>();
        }
    }

    protected MockFileSystem FileSystem { get; }

    protected IContainer Container { get; }

    protected SettingsManager SettingsManager => this.Container.Resolve<SettingsManager>();

    protected ILoggerFactory LoggerFactory { get; }

    public void Dispose()
    {
        this.Dispose(disposing: true);
        GC.SuppressFinalize(this);
    }

    protected string CreateTempSettingsFile(string fileName, string jsonContent)
    {
        var tempPath = this.FileSystem.Path.Combine(this.FileSystem.Path.GetTempPath(), fileName);
        var directory = this.FileSystem.Path.GetDirectoryName(tempPath);

        if (!string.IsNullOrEmpty(directory) && !this.FileSystem.Directory.Exists(directory))
        {
            _ = this.FileSystem.Directory.CreateDirectory(directory);
        }

        this.FileSystem.File.WriteAllText(tempPath, jsonContent);
        return tempPath;
    }

    protected string CreateMultiSectionSettingsFile(
        string fileName,
        IDictionary<string, object> sections,
        SettingsSourceMetadata? sourceMetadata = null,
        IDictionary<string, SettingsSectionMetadata>? sectionMetadata = null)
    {
        var document = new Dictionary<string, object>(StringComparer.Ordinal)
        {
            ["$meta"] = sourceMetadata ?? new SettingsSourceMetadata
            {
                WrittenAt = DateTimeOffset.UtcNow,
                WrittenBy = "TestHelper",
            },
        };

        // Add all sections with their metadata
        foreach (var section in sections)
        {
            var sectionContent = new Dictionary<string, object>(StringComparer.Ordinal);

            // Add section metadata if provided
            if (sectionMetadata != null && sectionMetadata.TryGetValue(section.Key, out var meta))
            {
                sectionContent["$meta"] = meta;
            }

            // Add section data
            if (section.Value is JsonElement jsonElement)
            {
                var deserialized = JsonSerializer.Deserialize<Dictionary<string, object>>(jsonElement.GetRawText());
                if (deserialized != null)
                {
                    foreach (var (key, value) in deserialized)
                    {
                        sectionContent[key] = value;
                    }
                }
            }
            else if (section.Value is Dictionary<string, object> dict)
            {
                foreach (var (key, value) in dict)
                {
                    sectionContent[key] = value;
                }
            }
            else
            {
                // For POCOs, serialize then deserialize to get properties
                var serialized = JsonSerializer.Serialize(section.Value, JsonOptions);
                var deserialized = JsonSerializer.Deserialize<Dictionary<string, object>>(serialized);
                if (deserialized != null)
                {
                    foreach (var (key, value) in deserialized)
                    {
                        sectionContent[key] = value;
                    }
                }
            }

            document[section.Key] = sectionContent;
        }

        return this.CreateTempSettingsFile(fileName, JsonSerializer.Serialize(document, JsonOptions));
    }

    protected virtual void Dispose(bool disposing)
    {
        if (!this.isDisposed)
        {
            if (disposing)
            {
                this.Container?.Dispose();
                this.LoggerFactory?.Dispose();
            }

            this.isDisposed = true;
        }
    }
}
