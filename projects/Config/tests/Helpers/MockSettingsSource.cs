// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.ObjectModel;
using System.Diagnostics.CodeAnalysis;

namespace DroidNet.Config.Tests.Helpers;

/// <summary>
/// Mock implementation of ISettingsSource for testing purposes.
/// </summary>
[ExcludeFromCodeCoverage]
public class MockSettingsSource : ISettingsSource
{
    private readonly Dictionary<string, object> sections = [];
    private readonly Dictionary<string, SettingsSectionMetadata> sectionMetadata = [];

    public MockSettingsSource(string id)
    {
        this.Id = id;

        // Silence the warning about the unused event
        _ = this.SourceChanged;
    }

    public event EventHandler<SourceChangedEventArgs>? SourceChanged;

    public string Id { get; }

    public bool SupportsEncryption { get; set; }

    public bool IsAvailable { get; set; } = true;

    public SettingsSourceMetadata? SourceMetadata { get; set; }

    public int ReadCallCount { get; private set; }

    public int WriteCallCount { get; private set; }

    public bool ShouldFailRead { get; set; }

    public bool ShouldFailWrite { get; set; }

    bool ISettingsSource.WatchForChanges { get => throw new NotSupportedException(); set => throw new NotSupportedException(); }

    public void AddSection(string sectionName, object data, SettingsSectionMetadata? metadata = null)
    {
        this.sections[sectionName] = data;
        if (metadata != null)
        {
            this.sectionMetadata[sectionName] = metadata;
        }
    }

    public void RemoveSection(string sectionName)
    {
        _ = this.sections.Remove(sectionName);
        _ = this.sectionMetadata.Remove(sectionName);
    }

    public void TriggerSourceChanged(SourceChangeType changeType)
        => this.SourceChanged?.Invoke(this, new SourceChangedEventArgs(this.Id, changeType));

    public Task<Result<SettingsReadPayload>> LoadAsync(bool reload = false, CancellationToken cancellationToken = default)
    {
        _ = cancellationToken; // unused

        this.ReadCallCount++;

        if (this.ShouldFailRead)
        {
            return Task.FromResult(Result.Fail<SettingsReadPayload>(new InvalidOperationException("Simulated read failure")));
        }

        var newSections = new ReadOnlyDictionary<string, object>(new Dictionary<string, object>(this.sections, StringComparer.Ordinal));
        var newSectionMetadata = new ReadOnlyDictionary<string, SettingsSectionMetadata>(new Dictionary<string, SettingsSectionMetadata>(this.sectionMetadata, StringComparer.Ordinal));
        var payload = new SettingsReadPayload(newSections, newSectionMetadata, this.SourceMetadata, this.Id);
        return Task.FromResult(Result.Ok(payload));
    }

    public Task<Result<SettingsWritePayload>> SaveAsync(
        IReadOnlyDictionary<string, object> sectionsData,
        IReadOnlyDictionary<string, SettingsSectionMetadata> sectionMetadata,
        SettingsSourceMetadata sourceMetadata,
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

        foreach (var meta in sectionMetadata)
        {
            this.sectionMetadata[meta.Key] = meta.Value;
        }

        this.SourceMetadata = sourceMetadata;

        var payload = new SettingsWritePayload(sourceMetadata, sectionsData.Count, this.Id);
        return Task.FromResult(Result.Ok(payload));
    }

    public Task<Result<SettingsValidationPayload>> ValidateAsync(
        IReadOnlyDictionary<string, object> sectionsData,
        CancellationToken cancellationToken = default)
    {
        var payload = new SettingsValidationPayload(sectionsData.Count, "Validation succeeded");
        return Task.FromResult(Result.Ok(payload));
    }
}
