// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.IO.Abstractions;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Projects.Mounts;

/// <summary>
/// Builds one mount table in the engine's order: the committed declaration, the project's own mounts file, the
/// committed paths, then the parent mounts files; then binds every mount and checks the source folders.
/// </summary>
/// <param name="fileSystem">The file system.</param>
/// <param name="projectRoot">The canonical project root.</param>
internal sealed class MountTableBuilder(IFileSystem fileSystem, FileSystemPath projectRoot)
{
    private readonly List<Entry> entries = [];
    private readonly List<MountFinding> findings = [];

    /// <summary>Adds the committed declaration.</summary>
    /// <param name="declared">The committed mounts.</param>
    public void Declare(IEnumerable<ProjectMount> declared)
    {
        foreach (var mount in declared)
        {
            if (!this.CheckName(mount.Name, null) || this.IsDuplicate(mount.Name, null))
            {
                continue;
            }

            var entry = new Entry(mount.Name, mount.Kind, MountScope.Declared, mount.Path, mount.IsExpanded);
            if (mount.Path is { } path && IsAbsolute(path))
            {
                this.Report(MountCodes.AbsoluteCommitted, $"Mount '{mount.Name}' commits the absolute path '{path}'; only .mounts.json may hold one.", mount.Name);
            }
            else if (mount.Path is { } relative)
            {
                entry.Committed = new Binding(relative, projectRoot, MountOrigin.Committed, null);
            }

            this.entries.Add(entry);
        }
    }

    /// <summary>Applies one mounts file: it binds, overrides or adds mounts as its level allows.</summary>
    /// <param name="file">The file.</param>
    public void Apply(MountsFile file)
    {
        foreach (var (name, path) in file.Entries)
        {
            var binding = new Binding(path, file.Path.Parent!, MountOrigin.MountsFile, file.Path);
            var entry = this.entries.FirstOrDefault(entry => string.Equals(entry.Name, name, StringComparison.OrdinalIgnoreCase));
            if (entry is null)
            {
                this.entries.Add(new Entry(name, MountKind.Source, MountScope.Local, null, IsExpanded: true) { Binding = binding });
            }
            else if (!string.Equals(entry.Name, name, StringComparison.Ordinal))
            {
                this.Report(MountCodes.NameDuplicate, $"'{file.Path}' names '{name}', which differs only in case from the mount '{entry.Name}'.", name, file.Path);
            }
            else if (file.IsParent && entry.DeclaredPath is not null)
            {
                this.Report(MountCodes.ParentIgnored, $"'{file.Path}' names '{name}', which the project commits a path for; the committed path is used.", name, file.Path);
            }
            else
            {
                entry.Binding ??= binding;
            }
        }
    }

    /// <summary>Gives each committed path to its mount when no project mounts file overrode it.</summary>
    public void ApplyCommitted()
    {
        foreach (var entry in this.entries.Where(static entry => entry.Committed is not null))
        {
            entry.Binding ??= entry.Committed;
        }
    }

    /// <summary>Binds every mount, checks the source folders and returns the table.</summary>
    /// <returns>The table.</returns>
    public ProjectMountTable Finish()
    {
        var mounts = this.entries.Select(this.Bind).ToList();
        this.CheckSourceFolders(mounts);
        return new ProjectMountTable(projectRoot, mounts, [.. this.findings]);
    }

    /// <summary>Checks the names of a mounts file: the name rule, reserved names and uniqueness ignoring case.</summary>
    /// <param name="names">The names.</param>
    /// <param name="file">The file.</param>
    /// <returns><see langword="true"/> when every name may be used.</returns>
    public bool CheckFileNames(IEnumerable<string> names, FileSystemPath file)
    {
        var valid = true;
        var seen = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        foreach (var name in names)
        {
            if (!this.CheckName(name, file))
            {
                valid = false;
            }
            else if (!seen.Add(name))
            {
                this.Report(MountCodes.NameDuplicate, $"'{file}' names '{name}' twice, ignoring case.", name, file);
                valid = false;
            }
        }

        return valid;
    }

    /// <summary>Records a finding.</summary>
    /// <param name="code">The code.</param>
    /// <param name="message">The message.</param>
    /// <param name="mount">The mount, if any.</param>
    /// <param name="file">The file, if any.</param>
    public void Report(string code, string message, string? mount = null, FileSystemPath? file = null)
        => this.findings.Add(new MountFinding(code, message, mount, file));

    private static bool IsAbsolute(string path)
        => Path.IsPathRooted(path.Replace('\\', '/'));

    private bool CheckName(string name, FileSystemPath? file)
    {
        if (ProjectMounts.IsValidName(name) && !ProjectAssetFiles.IsReservedMountName(name))
        {
            return true;
        }

        this.Report(MountCodes.NameInvalid, $"'{name}' is not a mount name: it breaks the name rule or is reserved.", name, file);
        return false;
    }

    private bool IsDuplicate(string name, FileSystemPath? file)
    {
        if (this.entries.FirstOrDefault(entry => string.Equals(entry.Name, name, StringComparison.OrdinalIgnoreCase)) is not { } existing)
        {
            return false;
        }

        this.Report(MountCodes.NameDuplicate, $"Mount '{name}' repeats the mount '{existing.Name}', ignoring case.", name, file);
        return true;
    }

    private ResolvedMount Bind(Entry entry)
    {
        var unbound = new ResolvedMount(entry.Name, entry.Kind, entry.Scope, entry.DeclaredPath, null, MountOrigin.None, null, MountState.Unbound, entry.IsExpanded);
        if (entry.Binding is not { } binding)
        {
            if (entry.DeclaredPath is null)
            {
                this.Report(MountCodes.Unbound, $"Mount '{entry.Name}' is a placeholder that no .mounts.json binds.", entry.Name);
            }

            return unbound;
        }

        var text = binding.Text.Replace('\\', '/');
        var location = MountFolders.Canonical(fileSystem, IsAbsolute(text) ? text : Path.Combine(binding.Base.FullPath, text));
        var isFolder = fileSystem.Directory.Exists(location.FullPath);
        var exists = isFolder || fileSystem.File.Exists(location.FullPath);
        var mount = unbound with { Location = location, Origin = binding.Origin, OriginFile = binding.File, State = exists ? MountState.Bound : MountState.Missing };
        if (!exists)
        {
            this.Report(MountCodes.FolderMissing, $"Mount '{entry.Name}' is bound to '{location}', which does not exist.", entry.Name, binding.File);
        }
        else if ((entry.Kind == MountKind.Pak) == isFolder)
        {
            this.Report(MountCodes.PathKind, $"{entry.Kind} mount '{entry.Name}' is bound to the {(isFolder ? "folder" : "file")} '{location}'.", entry.Name, binding.File);
        }

        return mount;
    }

    private void CheckSourceFolders(List<ResolvedMount> mounts)
    {
        var sources = mounts.Where(static mount => mount is { Kind: MountKind.Source, Location: not null }).ToArray();
        for (var index = 0; index < sources.Length; index++)
        {
            var mount = sources[index];
            if (MountFolders.IsInOutputFolder(mount.Location!, projectRoot))
            {
                this.Report(MountCodes.FolderInOutput, $"Mount '{mount.Name}' is in an output folder: '{mount.Location}'.", mount.Name, mount.OriginFile);
            }

            foreach (var other in sources[(index + 1)..].Where(other => MountFolders.SegmentsBelow(mount.Location!, other.Location!) is not null
                || MountFolders.SegmentsBelow(other.Location!, mount.Location!) is not null))
            {
                this.Report(MountCodes.FolderOverlap, $"Mount '{other.Name}' at '{other.Location}' overlaps mount '{mount.Name}' at '{mount.Location}'.", other.Name, other.OriginFile);
            }
        }
    }

    private sealed record Binding(string Text, FileSystemPath Base, MountOrigin Origin, FileSystemPath? File);

    private sealed record Entry(string Name, MountKind Kind, MountScope Scope, string? DeclaredPath, bool IsExpanded)
    {
        public Binding? Committed { get; set; }

        public Binding? Binding { get; set; }
    }
}
