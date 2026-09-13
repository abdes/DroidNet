// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using System.Security.Cryptography;

namespace Oxygen.Editor.ContentPipeline.Incremental;

/// <summary>Protects the exact output bytes inspected and validated by the native adapter.</summary>
internal sealed partial class CookOutputReadLease : IAsyncDisposable, IDisposable
{
    private readonly string root;
    private readonly Dictionary<string, FileStream> files = [with(StringComparer.Ordinal)];
    private Dictionary<string, CookProvenance.FileProof>? hashes;

    private CookOutputReadLease(string root) => this.root = root;

    /// <summary>Gets a value indicating whether the adapter produced a physical native index.</summary>
    public bool HasIndex => this.files.ContainsKey("container.index.bin");

    /// <summary>Opens all existing output files without permitting replacement during validation.</summary>
    /// <param name="root">The physical cooked root.</param>
    /// <param name="cancellationToken">Cancels acquisition.</param>
    /// <param name="requireIndex">Whether an absent index should yield an empty lease; explicit reports also inspect incomplete roots.</param>
    /// <returns>The acquired lease; an adapter without physical output yields an empty lease.</returns>
    public static async Task<CookOutputReadLease> AcquireAsync(string root, CancellationToken cancellationToken, bool requireIndex = true)
    {
        if (!Directory.Exists(root) || (requireIndex && !File.Exists(Path.Combine(root, "container.index.bin"))))
        {
            return new(root);
        }

        var lease = new CookOutputReadLease(root);
        try
        {
            foreach (var path in Directory.EnumerateFiles(root, "*", SearchOption.AllDirectories))
            {
                cancellationToken.ThrowIfCancellationRequested();
                lease.files.Add(Path.GetRelativePath(root, path).Replace('\\', '/'), new(path, FileMode.Open, FileAccess.Read, FileShare.Read, 65536, FileOptions.Asynchronous | FileOptions.SequentialScan));
            }

            lease.CheckMembership();
            return lease;
        }
        catch
        {
            await lease.DisposeAsync().ConfigureAwait(false);
            throw;
        }
    }

    /// <summary>Returns the actual protected file set, including files not represented by index file records.</summary>
    /// <returns>Root-relative paths and sizes without reading file contents.</returns>
    public IReadOnlyList<CookedFileEntry> GetFiles()
    {
        this.CheckMembership();
        return this.files.OrderBy(static pair => pair.Key, StringComparer.Ordinal)
            .Select(static pair => new CookedFileEntry(pair.Key, checked((ulong)pair.Value.Length))).ToArray();
    }

    /// <summary>Captures output identities after successful validation of the protected bytes.</summary>
    /// <param name="mount">The physical mount name.</param>
    /// <param name="inspection">The native index entries.</param>
    /// <param name="cancellationToken">Cancels hashing.</param>
    /// <returns>The complete root proof.</returns>
    public async Task<CookProvenance.Root> CaptureAsync(string mount, CookInspectionResult inspection, CancellationToken cancellationToken)
    {
        var proofs = await this.ReadHashesAsync(cancellationToken).ConfigureAwait(false);
        var assets = inspection.Assets.Select(asset => new CookProvenance.IndexedAsset(asset, proofs[asset.DescriptorRelativePath ?? throw new InvalidDataException("The native index omitted a descriptor path.")])).ToImmutableArray();
        var descriptors = assets.Select(static asset => asset.File.RelativePath).ToHashSet(StringComparer.Ordinal);
        this.CheckMembership();
        return new(mount, [.. proofs.Values.Where(file => !descriptors.Contains(file.RelativePath)).OrderBy(static file => file.RelativePath, StringComparer.Ordinal)], assets);
    }

    /// <summary>Hashes one protected set of files for validation or reuse without mixing root generations.</summary>
    /// <param name="cancellationToken">Cancels hashing.</param>
    /// <returns>The root-relative file identities.</returns>
    public async Task<IReadOnlyDictionary<string, CookProvenance.FileProof>> ReadHashesAsync(CancellationToken cancellationToken)
    {
        if (this.hashes is not null)
        {
            return this.hashes;
        }

        var proofs = new Dictionary<string, CookProvenance.FileProof>(StringComparer.Ordinal);
        foreach (var (relative, stream) in this.files)
        {
            stream.Position = 0;
            var hash = Convert.ToHexString(await SHA256.HashDataAsync(stream, cancellationToken).ConfigureAwait(false));
            proofs.Add(relative, new(relative, stream.Length, hash));
        }

        this.CheckMembership();
        this.hashes = proofs;
        return proofs;
    }

    /// <summary>Verifies indexed descriptors against the bytes protected by this reader.</summary>
    /// <param name="records">Indexed asset identities and descriptor hashes.</param>
    /// <param name="cancellationToken">Cancels verification.</param>
    /// <returns>Completion after each recorded descriptor matches its digest and length.</returns>
    public async Task VerifyDescriptorsAsync(IReadOnlyList<Oxygen.Managed.Assets.Catalog.AssetRecord> records, CancellationToken cancellationToken)
    {
        foreach (var record in records)
        {
            cancellationToken.ThrowIfCancellationRequested();
            var metadata = record.Cooked ?? throw new InvalidDataException("The cooked index omitted asset metadata.");
            if (!this.files.TryGetValue(metadata.DescriptorRelativePath, out var stream) || (ulong)stream.Length != metadata.DescriptorSize)
            {
                throw new InvalidDataException($"Cooked descriptor is missing or has changed: {metadata.DescriptorRelativePath}.");
            }

            stream.Position = 0;
            var hash = Convert.ToHexString(await SHA256.HashDataAsync(stream, cancellationToken).ConfigureAwait(false));
            if (!string.Equals(hash, metadata.DescriptorSha256, StringComparison.OrdinalIgnoreCase))
            {
                throw new InvalidDataException($"Cooked descriptor failed its integrity check: {metadata.DescriptorRelativePath}.");
            }
        }

        this.CheckMembership();
    }

    /// <inheritdoc />
    public void Dispose()
    {
        foreach (var stream in this.files.Values)
        {
            stream.Dispose();
        }
    }

    /// <inheritdoc />
    public async ValueTask DisposeAsync()
    {
        foreach (var stream in this.files.Values)
        {
            await stream.DisposeAsync().ConfigureAwait(false);
        }
    }

    private void CheckMembership()
    {
        var current = Directory.EnumerateFiles(this.root, "*", SearchOption.AllDirectories).Select(path => Path.GetRelativePath(this.root, path).Replace('\\', '/')).ToHashSet(StringComparer.Ordinal);
        if (!current.SetEquals(this.files.Keys))
        {
            throw new IOException("Cooked output changed while its files were being validated.");
        }
    }
}
