// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ContentPipeline.Publication;

/// <summary>Physical lifetime operations for immutable, project-owned cooked generations.</summary>
internal sealed class CookedGeneration : IDisposable
{
    internal const string MarkerFileName = ".generation.lock";

    private readonly string root;
    private readonly FileStream? ownership;
    private readonly bool abandoned;
    private bool disposed;

    private CookedGeneration(string root, FileStream? ownership, bool abandoned = false)
    {
        this.root = root;
        this.ownership = ownership;
        this.abandoned = abandoned;
    }

    /// <summary>Claims a journaled candidate after its operation has drained and recovery has settled.</summary>
    internal static CookedGeneration? TryClaimAbandoned(string root)
    {
        CookOutputLease.RejectReparsePoint(root);
        if (!Directory.Exists(root))
        {
            return null;
        }

        var marker = Path.Combine(root, MarkerFileName);
        CookOutputLease.RejectReparsePoint(marker);
        return File.Exists(marker) ? TryClaim(root) : new(root, ownership: null, abandoned: true);
    }

    /// <summary>Claims an unselected generation before releasing the publication gate.</summary>
    /// <param name="root">A contained generation already excluded from selected, active and recovery roots.</param>
    /// <returns>Ownership retained through deletion, or null for absent, live or nonempty unsealed roots.</returns>
    internal static CookedGeneration? TryClaim(string root)
    {
        CookOutputLease.RejectReparsePoint(root);
        if (!Directory.Exists(root))
        {
            return null;
        }

        var marker = Path.Combine(root, MarkerFileName);
        CookOutputLease.RejectReparsePoint(marker);
        var ownership = WindowsCookFile.TryClaimGeneration(marker);
        try
        {
            // Nonempty unsealed roots belong to operation recovery, not GC.
            return ownership is null && Directory.EnumerateFileSystemEntries(root).Any()
                ? null : new(root, ownership);
        }
        catch
        {
            ownership?.Dispose();
            throw;
        }
    }

    /// <summary>Removes payloads outside the selection gate while keeping the exclusive claim.</summary>
    internal void Delete()
    {
        ObjectDisposedException.ThrowIf(this.disposed, this);
        if (!Directory.Exists(this.root))
        {
            return;
        }

        if (this.ownership is null && !this.abandoned)
        {
            // An interrupted final removal can leave an empty shell. Never
            // recurse without a marker claim if another file has appeared.
            Directory.Delete(this.root);
            return;
        }

        var marker = Path.Combine(this.root, MarkerFileName);
        var index = Path.Combine(this.root, "container.index.bin");
        CookOutputLease.RejectReparsePoint(index);
        File.Delete(index);
        foreach (var entry in Directory.EnumerateFileSystemEntries(this.root))
        {
            if (string.Equals(Path.GetFileName(entry), MarkerFileName, StringComparison.OrdinalIgnoreCase))
            {
                continue;
            }

            CookOutputLease.RejectReparsePoint(entry);
            if (Directory.Exists(entry))
            {
                DeletePayloadDirectory(entry);
            }
            else
            {
                File.Delete(entry);
            }
        }

        // Keep the marker through every fallible payload deletion so a partial
        // cleanup remains claimable on the next maintenance pass.
        File.Delete(marker);
        Directory.Delete(this.root);
    }

    /// <inheritdoc />
    public void Dispose()
    {
        if (!this.disposed)
        {
            this.disposed = true;
            this.ownership?.Dispose();
        }
    }

    private static void DeletePayloadDirectory(string directory)
    {
        foreach (var entry in Directory.EnumerateFileSystemEntries(directory))
        {
            CookOutputLease.RejectReparsePoint(entry);
            if (Directory.Exists(entry))
            {
                DeletePayloadDirectory(entry);
            }
            else
            {
                File.Delete(entry);
            }
        }

        Directory.Delete(directory);
    }
}
