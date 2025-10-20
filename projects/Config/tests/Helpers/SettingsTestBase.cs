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

    protected SettingsTestBase(bool registerDefaultSettingsServices = true)
    {
        this.FileSystem = new MockFileSystem();
        this.Container = new Container();
        this.LoggerFactory = new LoggerFactory();

        // Register common services
        this.Container.RegisterInstance<Microsoft.Extensions.Logging.ILoggerFactory>(this.LoggerFactory);
        this.Container.RegisterInstance<System.IO.Abstractions.IFileSystem>(this.FileSystem);

        // Register default settings services if not disabled
        if (registerDefaultSettingsServices)
        {
            // Register settings services using factory delegates that resolve the SettingsManager
            this.Container.RegisterDelegate<ISettingsService<ITestSettings>>(
                resolver => new TestSettingsService(
                    resolver.Resolve<SettingsManager>(),
                    resolver.Resolve<ILoggerFactory>()),
                Reuse.Singleton);

            this.Container.RegisterDelegate<ISettingsService<IAlternativeTestSettings>>(
                resolver => new AlternativeTestSettingsService(
                    resolver.Resolve<SettingsManager>(),
                    resolver.Resolve<ILoggerFactory>()),
                Reuse.Singleton);

            this.Container.RegisterDelegate<ISettingsService<IInvalidTestSettings>>(
                resolver => new InvalidTestSettingsService(
                    resolver.Resolve<SettingsManager>(),
                    resolver.Resolve<ILoggerFactory>()),
                Reuse.Singleton);
        }
    }

    protected MockFileSystem FileSystem { get; }

    protected IContainer Container { get; }

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

    protected string CreateMultiSectionSettingsFile(string fileName, IDictionary<string, object> sections, SettingsMetadata? metadata = null)
    {
        var document = new Dictionary<string, object>(StringComparer.Ordinal)
        {
            ["metadata"] = metadata ?? new SettingsMetadata { Version = "1.0", SchemaVersion = "20251019" },
        };

        // Add all sections directly to the root document (flat structure)
        foreach (var section in sections)
        {
            document[section.Key] = section.Value;
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
