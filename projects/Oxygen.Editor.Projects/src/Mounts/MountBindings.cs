// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using System.Text;
using System.Text.Json;
using System.Text.Json.Nodes;
using DroidNet.Storage;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Projects.Mounts;

/// <summary>
/// Changes of the project's own <c>.mounts.json</c>, the file at its root, which binds and overrides the project's mounts
/// on this machine: each mount's new folder or pak file, or the removal of its binding.
/// </summary>
/// <remarks>
/// Only the project's own file is written. A <c>.mounts.json</c> above the project belongs to the studio or the
/// machine, and is never changed for one project. The file keeps what it holds besides the changed bindings.
/// </remarks>
/// <param name="Changes">Each changed mount's absolute folder or pak file, or <see langword="null"/> to remove its
/// binding, by mount name.</param>
public sealed record MountBindings(ImmutableSortedDictionary<string, string?> Changes)
{
    private static readonly JsonSerializerOptions JsonOptions = new() { WriteIndented = true };

    /// <summary>Gets changes that change nothing.</summary>
    public static MountBindings None { get; } = new(ImmutableSortedDictionary.Create<string, string?>(StringComparer.Ordinal));

    /// <summary>Gets a value indicating whether there is no change.</summary>
    public bool IsEmpty => this.Changes.IsEmpty;

    /// <summary>Gets the changes with one mount bound to a folder or pak file.</summary>
    /// <param name="name">The mount name.</param>
    /// <param name="location">The absolute folder or pak file.</param>
    /// <returns>The changes.</returns>
    public MountBindings Bind(string name, FileSystemPath location)
    {
        ArgumentException.ThrowIfNullOrEmpty(name);
        ArgumentNullException.ThrowIfNull(location);
        return new(this.Changes.SetItem(name, location.FullPath.Replace('\\', '/')));
    }

    /// <summary>Gets the changes with one mount's binding removed.</summary>
    /// <param name="name">The mount name.</param>
    /// <returns>The changes.</returns>
    public MountBindings Unbind(string name)
    {
        ArgumentException.ThrowIfNullOrEmpty(name);
        return new(this.Changes.SetItem(name, null));
    }

    /// <summary>Writes the changes into the project's own mounts file, creating it when it does not exist.</summary>
    /// <param name="files">The atomic file store, which refuses a file that changed since it was read.</param>
    /// <param name="projectRoot">The project root.</param>
    /// <param name="cancellationToken">Cancels the write.</param>
    /// <returns>The changes that undo this write: each changed mount's previous binding, or its removal.</returns>
    /// <exception cref="InvalidDataException">The existing file is not a mounts file of schema_version 2.</exception>
    /// <exception cref="StorageWriteConflictException">The file changed while it was being written.</exception>
    public async Task<MountBindings> WriteAsync(IAtomicFileStore files, FileSystemPath projectRoot, CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(files);
        ArgumentNullException.ThrowIfNull(projectRoot);
        if (this.IsEmpty)
        {
            return None;
        }

        var path = projectRoot.Combine(ProjectMounts.FileName).FullPath;
        var snapshot = await files.ReadAsync(path, cancellationToken).ConfigureAwait(false);
        var document = snapshot.Version.Exists ? Read(path, snapshot.Content) : new JsonObject { ["schema_version"] = 2 };
        if (document["mounts"] is not JsonObject mounts)
        {
            mounts = [];
            document["mounts"] = mounts;
        }

        var undo = this.Changes.Keys.ToImmutableSortedDictionary(
            static name => name,
            name => mounts[name] is JsonValue previous && previous.TryGetValue<string>(out var folder) ? folder : null,
            StringComparer.Ordinal);
        foreach (var (name, location) in this.Changes)
        {
            if (location is null)
            {
                _ = mounts.Remove(name);
            }
            else
            {
                mounts[name] = location;
            }
        }

        var bytes = Encoding.UTF8.GetBytes(document.ToJsonString(JsonOptions).ReplaceLineEndings("\n") + "\n");
        _ = await files.WriteAsync(path, bytes, snapshot.Version, cancellationToken).ConfigureAwait(false);
        return new(undo);
    }

    private static JsonObject Read(string path, ImmutableArray<byte> content)
    {
        try
        {
            return JsonNode.Parse(content.AsSpan()) is JsonObject document
                && document["schema_version"] is JsonValue version && version.TryGetValue<int>(out var number) && number == 2
                    ? document
                    : throw new InvalidDataException($"'{path}' is not a mounts file of schema_version 2; fix it before binding a mount.");
        }
        catch (JsonException exception)
        {
            throw new InvalidDataException($"'{path}' cannot be read as JSON; fix it before binding a mount.", exception);
        }
    }
}
