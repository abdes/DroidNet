// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using System.Text.Json;

namespace DroidNet.Config.Tests.TestHelpers;

/// <summary>
/// Mock implementation of ISettingsSource for testing purposes.
/// </summary>
[ExcludeFromCodeCoverage]
public class MockSettingsSource : ISettingsSource, IDisposable
{
    private readonly Dictionary<string, object> sections = new();
    private bool isDisposed;

    public MockSettingsSource(string id)
    {
        this.Id = id;
    }

    public event EventHandler<SettingsSourceChangedEventArgs>? Changed;

    public event EventHandler<SourceErrorEventArgs>? Error;

    public string Id { get; }

    public bool CanWrite { get; set; } = true;

    public bool SupportsEncryption { get; set; } = false;

    public bool IsAvailable { get; set; } = true;

    public SettingsMetadata? Metadata { get; set; }

    public int ReadCallCount { get; private set; }

    public int WriteCallCount { get; private set; }

    public bool ShouldFailRead { get; set; }

    public bool ShouldFailWrite { get; set; }

    public void AddSection(string sectionName, object data)
    {
        this.sections[sectionName] = data;
    }

    public Task<SettingsSourceReadResult> ReadAsync(CancellationToken cancellationToken = default)
    {
        this.ReadCallCount++;

        if (this.ShouldFailRead)
        {
            return Task.FromResult(SettingsSourceReadResult.CreateFailure("Simulated read failure"));
        }

        var result = SettingsSourceReadResult.CreateSuccess(this.sections, this.Metadata);
        return Task.FromResult(result);
    }

    public Task<SettingsSourceWriteResult> WriteAsync(
        IReadOnlyDictionary<string, object> sectionsData,
        SettingsMetadata metadata,
        CancellationToken cancellationToken = default)
    {
        this.WriteCallCount++;

        if (this.ShouldFailWrite)
        {
            return Task.FromResult(SettingsSourceWriteResult.CreateFailure("Simulated write failure"));
        }

        foreach (var section in sectionsData)
        {
            this.sections[section.Key] = section.Value;
        }

        this.Metadata = metadata;

        return Task.FromResult(SettingsSourceWriteResult.CreateSuccess());
    }

    public Task<SettingsSourceResult> ValidateAsync(
        IReadOnlyDictionary<string, object> sectionsData,
        CancellationToken cancellationToken = default)
    {
        // Mock implementation - always succeed
        return Task.FromResult(SettingsSourceResult.CreateSuccess("Validation succeeded"));
    }

    public Task<SettingsSourceResult> ReloadAsync(CancellationToken cancellationToken = default)
    {
        this.ReadCallCount++;

        if (this.ShouldFailRead)
        {
            return Task.FromResult(SettingsSourceResult.CreateFailure("Simulated reload failure"));
        }

        return Task.FromResult(SettingsSourceResult.CreateSuccess("Reload succeeded"));
    }

    public IDisposable? WatchForChanges(Action<string> changeHandler)
    {
        // Mock implementation - return a simple disposable
        return new MockDisposable();
    }

    public void EnableWatching()
    {
        // No-op for mock
    }

    public void DisableWatching()
    {
        // No-op for mock
    }

    public void TriggerChange(SettingsSourceChangeType changeType)
    {
        this.Changed?.Invoke(this, new SettingsSourceChangedEventArgs(this.Id, changeType));
    }

    public void TriggerError(string errorMessage, Exception? exception = null)
    {
        this.Error?.Invoke(this, new SourceErrorEventArgs(this.Id, errorMessage, exception));
    }

    public void Dispose()
    {
        if (!this.isDisposed)
        {
            this.sections.Clear();
            this.isDisposed = true;
        }

        GC.SuppressFinalize(this);
    }

    private class MockDisposable : IDisposable
    {
        public void Dispose()
        {
            // No-op
        }
    }
}
