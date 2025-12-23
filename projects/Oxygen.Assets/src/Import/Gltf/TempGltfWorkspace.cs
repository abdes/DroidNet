// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Assets.Import.Gltf;

internal sealed class TempGltfWorkspace : IAsyncDisposable
{
    private readonly string root;

    public TempGltfWorkspace(string sourcePath)
    {
        this.root = Path.Combine(Path.GetTempPath(), "Oxygen.Assets", "gltf", Guid.NewGuid().ToString("N"));
        this.GltfPath = Path.Combine(this.root, Path.GetFileName(sourcePath));
    }

    public string GltfPath { get; }

    public async ValueTask WriteGltfAsync(ReadOnlyMemory<byte> gltfJsonUtf8, CancellationToken cancellationToken)
    {
        Directory.CreateDirectory(this.root);
        await File.WriteAllBytesAsync(this.GltfPath, gltfJsonUtf8.ToArray(), cancellationToken).ConfigureAwait(false);
    }

    public async Task<bool> TryWriteReferencedResourceToTempAsync(
        ImportContext context,
        string resolvedSourceDep,
        string safeRelative,
        CancellationToken cancellationToken)
    {
        try
        {
            var bytes = await context.Files.ReadAllBytesAsync(resolvedSourceDep, cancellationToken).ConfigureAwait(false);
            var outPath = this.GetSafeTempOutputPathOrThrow(safeRelative);
            var outDir = Path.GetDirectoryName(outPath);
            if (!string.IsNullOrWhiteSpace(outDir))
            {
                Directory.CreateDirectory(outDir);
            }

            await File.WriteAllBytesAsync(outPath, bytes.ToArray(), cancellationToken).ConfigureAwait(false);
            return true;
        }
        catch (FileNotFoundException)
        {
            return HandleMissingResource(context, resolvedSourceDep, safeRelative);
        }
        catch (DirectoryNotFoundException)
        {
            return HandleMissingResource(context, resolvedSourceDep, safeRelative);
        }
    }

    public ValueTask DisposeAsync()
    {
        this.TryDeleteDirectory();
        return ValueTask.CompletedTask;
    }

    private static bool HandleMissingResource(ImportContext context, string resolvedSourceDep, string safeRelative)
    {
        context.Diagnostics.Add(
            ImportDiagnosticSeverity.Error,
            code: "OXYIMPORT_GLTF_MISSING_RESOURCE",
            message: $"Missing referenced glTF resource '{safeRelative}'.",
            sourcePath: resolvedSourceDep);

        return context.Options.FailFast
            ? throw new FileNotFoundException("Missing referenced glTF resource.", resolvedSourceDep)
            : false;
    }

    private string GetSafeTempOutputPathOrThrow(string safeRelative)
    {
        var rootFull = Path.GetFullPath(this.root).TrimEnd(Path.DirectorySeparatorChar) + Path.DirectorySeparatorChar;
        var combined = Path.Combine(this.root, safeRelative.Replace('/', Path.DirectorySeparatorChar));
        var full = Path.GetFullPath(combined);
        return !full.StartsWith(rootFull, StringComparison.OrdinalIgnoreCase)
            ? throw new NotSupportedException($"glTF uri resolves outside temp root: '{safeRelative}'.")
            : full;
    }

    private void TryDeleteDirectory()
    {
        try
        {
            if (Directory.Exists(this.root))
            {
                Directory.Delete(this.root, recursive: true);
            }
        }
        catch (IOException)
        {
            // Best-effort cleanup.
        }
        catch (UnauthorizedAccessException)
        {
            // Best-effort cleanup.
        }
    }
}
