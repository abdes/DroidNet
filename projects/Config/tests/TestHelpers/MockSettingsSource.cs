// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.ObjectModel;
using System.Diagnostics.CodeAnalysis;

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

    public event EventHandler<SourceErrorEventArgs>? Error;

    public event EventHandler<SourceChangedEventArgs>? SourceChanged;

    public string Id { get; }

    public bool CanWrite { get; set; } = true;

    public bool SupportsEncryption { get; set; }

    public bool IsAvailable { get; set; } = true;

    public SettingsMetadata? Metadata { get; set; }

    public int ReadCallCount { get; private set; }

    public int WriteCallCount { get; private set; }

    public bool ShouldFailRead { get; set; }

    public bool ShouldFailWrite { get; set; }

    bool ISettingsSource.WatchForChanges { get => throw new NotSupportedException(); set => throw new NotSupportedException(); }

    public void AddSection(string sectionName, object data)
    {
        this.sections[sectionName] = data;
    }

    public Task<Result<SettingsReadPayload>> LoadAsync(CancellationToken cancellationToken = default)
    {
        this.ReadCallCount++;

        if (this.ShouldFailRead)
        {
            return Task.FromResult(Result.Fail<SettingsReadPayload>(new InvalidOperationException("Simulated read failure")));
        }

        var sections = new ReadOnlyDictionary<string, object>(new Dictionary<string, object>(this.sections));
        var payload = new SettingsReadPayload(sections, this.Metadata, this.Id);
        return Task.FromResult(Result.Ok(payload));
    }

    public Task<Result<SettingsWritePayload>> SaveAsync(
        IReadOnlyDictionary<string, object> sectionsData,
        SettingsMetadata metadata,
        CancellationToken cancellationToken = default)
    {
        this.WriteCallCount++;

        if (this.ShouldFailWrite)
        {
            return Task.FromResult(Result.Fail<SettingsWritePayload>(new InvalidOperationException("Simulated write failure")));
        }

        foreach (var section in sectionsData)
        {
            this.sections[section.Key] = section.Value;
        }

        this.Metadata = metadata;

        var payload = new SettingsWritePayload(metadata, sectionsData.Count, this.Id);
        return Task.FromResult(Result.Ok(payload));
    }

    public Task<Result<SettingsValidationPayload>> ValidateAsync(
        IReadOnlyDictionary<string, object> sectionsData,
        CancellationToken cancellationToken = default)
    {
        var payload = new SettingsValidationPayload(sectionsData.Count, "Validation succeeded");
        return Task.FromResult(Result.Ok(payload));
    }

    public Task<Result<SettingsValidationPayload>> ReloadAsync(CancellationToken cancellationToken = default)
    {
        this.ReadCallCount++;

        if (this.ShouldFailRead)
        {
            return Task.FromResult(Result.Fail<SettingsValidationPayload>(new InvalidOperationException("Simulated reload failure")));
        }

        var payload = new SettingsValidationPayload(this.sections.Count, "Reload succeeded");
        return Task.FromResult(Result.Ok(payload));
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

    public void TriggerChange(SourceChangeType changeType)
    {
        this.SourceChanged?.Invoke(this, new SourceChangedEventArgs(this.Id, changeType));
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

    public Task<Result<SettingsReadPayload>> LoadAsync(bool reload = false, CancellationToken cancellationToken = default)
    {
        // For the mock, the reload flag is ignored. Delegate to the primary LoadAsync implementation.
        return this.LoadAsync(cancellationToken);
    }

    private class MockDisposable : IDisposable
    {
        public void Dispose()
        {
            // No-op
        }
    }
}
