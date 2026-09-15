// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ContentPipeline.Publication;

/// <summary>Lets publication drain finite status inspections without accepting live preview readers.</summary>
public static partial class CookOutputLease
{
    /// <summary>Registers a persistent reader after a competing gate owner releases it.</summary>
    /// <param name="projectRoot">The owning project directory.</param>
    /// <param name="cancellationToken">Cancels waiting before reader ownership is acquired.</param>
    /// <returns>A lease retained through native mounting and subsequent reads.</returns>
    public static async Task<IDisposable> AcquireReadAsync(string projectRoot, CancellationToken cancellationToken)
    {
        var (_, gatePath, readers) = PreparePaths(projectRoot);
        var gate = await OpenGateAsync(gatePath, cancellationToken).ConfigureAwait(false);
        await using var gateLifetime = gate.ConfigureAwait(false);
        return CreateReader(readers);
    }

    /// <summary>Protects a finite metadata and output inspection, yielding to an existing publisher.</summary>
    /// <param name="projectRoot">The owning project directory.</param>
    /// <param name="cancellationToken">Cancels waiting for publication.</param>
    /// <returns>A marker released after the inspected file handles close.</returns>
    internal static async Task<IDisposable> AcquireInspectionAsync(string projectRoot, CancellationToken cancellationToken)
    {
        var (_, gatePath, readers) = PreparePaths(projectRoot);
        var gate = await OpenGateAsync(gatePath, cancellationToken).ConfigureAwait(false);
        await using var gateLifetime = gate.ConfigureAwait(false);
        return CreateReader(readers, ".scan");
    }

    /// <summary>Excludes new readers, rejects live previews, and drains already registered status scans.</summary>
    /// <param name="projectRoot">The owning project directory.</param>
    /// <param name="cancellationToken">Cancels before any output replacement starts.</param>
    /// <returns>The exclusive publication lease.</returns>
    internal static async Task<CookOutputWriteLease> AcquireWriteAsync(string projectRoot, CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        var (root, gatePath, readers) = PreparePaths(projectRoot);
        var writer = new CookOutputWriteLease(root, readers, await OpenGateAsync(gatePath, cancellationToken).ConfigureAwait(false));
        try
        {
            RequireReleasedReaders(readers);
            while (true)
            {
                cancellationToken.ThrowIfCancellationRequested();
                if (TryRemoveReleasedReaders(readers, "*.scan"))
                {
                    break;
                }

                await Task.Delay(50, cancellationToken).ConfigureAwait(false);
            }

            return writer;
        }
        catch
        {
            writer.Dispose();
            throw;
        }
    }

    private static async Task<FileStream> OpenGateAsync(string path, CancellationToken cancellationToken)
    {
        while (true)
        {
            cancellationToken.ThrowIfCancellationRequested();
            var gate = WindowsCookFile.TryOpenExclusive(path, FileMode.OpenOrCreate, out _);
            if (gate is not null)
            {
                return gate;
            }

            await Task.Delay(50, cancellationToken).ConfigureAwait(false);
        }
    }
}
