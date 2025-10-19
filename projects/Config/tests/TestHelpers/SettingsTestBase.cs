// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using DryIoc;
using Microsoft.Extensions.Logging;
using Testably.Abstractions.Testing;

namespace DroidNet.Config.Tests.TestHelpers;

/// <summary>
/// Base class for tests that require a properly configured DI container with settings services.
/// </summary>
[ExcludeFromCodeCoverage]
public abstract class SettingsTestBase : IDisposable
{
    private bool isDisposed;

    protected SettingsTestBase()
    {
        this.FileSystem = new MockFileSystem();
        this.Container = new Container();
        this.LoggerFactory = new LoggerFactory();

        // Register common services
        this.Container.RegisterInstance<Microsoft.Extensions.Logging.ILoggerFactory>(this.LoggerFactory);
        this.Container.RegisterInstance<System.IO.Abstractions.IFileSystem>(this.FileSystem);

        // Register default settings services if not disabled
        if (this.RegisterDefaultSettingsServices)
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

    /// <summary>
    /// Gets a value indicating whether default settings service registrations should be performed.
    /// Override this property in derived classes to control automatic service registration.
    /// </summary>
    protected virtual bool RegisterDefaultSettingsServices => true;

    protected MockFileSystem FileSystem { get; }

    protected IContainer Container { get; }

    protected ILoggerFactory LoggerFactory { get; }

    /// <summary>
    /// Creates a temporary JSON settings file with the specified content.
    /// </summary>
    protected string CreateTempSettingsFile(string fileName, string jsonContent)
    {
        var tempPath = this.FileSystem.Path.Combine(this.FileSystem.Path.GetTempPath(), fileName);
        var directory = this.FileSystem.Path.GetDirectoryName(tempPath);

        if (!string.IsNullOrEmpty(directory) && !this.FileSystem.Directory.Exists(directory))
        {
            this.FileSystem.Directory.CreateDirectory(directory);
        }

        this.FileSystem.File.WriteAllText(tempPath, jsonContent);
        return tempPath;
    }

    /// <summary>
    /// Creates a settings file with proper multi-section structure.
    /// </summary>
    protected string CreateMultiSectionSettingsFile(string fileName, Dictionary<string, object> sections, SettingsMetadata? metadata = null)
    {
        var document = new Dictionary<string, object>
        {
            ["metadata"] = metadata ?? new SettingsMetadata { Version = "1.0", SchemaVersion = "20251019" }
        };

        // Add all sections directly to the root document (flat structure)
        foreach (var section in sections)
        {
            document[section.Key] = section.Value;
        }

        // DO NOT use camelCase for root-level properties - preserve section names!
        var json = System.Text.Json.JsonSerializer.Serialize(document, new System.Text.Json.JsonSerializerOptions
        {
            WriteIndented = true,
        });

        return this.CreateTempSettingsFile(fileName, json);
    }

    public void Dispose()
    {
        this.Dispose(true);
        GC.SuppressFinalize(this);
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
