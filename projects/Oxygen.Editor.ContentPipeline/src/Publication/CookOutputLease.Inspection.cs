// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ContentPipeline.Publication;

/// <summary>Lets publication drain finite status inspections without accepting live preview readers.</summary>
public static partial class CookOutputLease
{
    /// <summary>Protects a finite metadata and output inspection, yielding to an existing publisher.</summary>
    /// <param name="projectRoot">The owning project directory.</param>
    /// <param name="cancellationToken">Cancels waiting for publication.</param>
    /// <returns>A marker released after the inspected file handles close.</returns>
    internal static async Task<IDisposable> AcquireInspectionAsync(string projectRoot, CancellationToken cancellationToken)
    {
        var (_, gatePath, readers) = PreparePaths(projectRoot);
        while (true)
        {
            cancellationToken.ThrowIfCancellationRequested();
            try
            {
                var gate = OpenGate(gatePath);
                await using var gateLifetime = gate.ConfigureAwait(false);
                return CreateReader(readers, ".scan");
            }
            catch (CookOutputBusyException)
            {
                await Task.Delay(50, cancellationToken).ConfigureAwait(false);
            }
        }
    }

    /// <summary>Excludes new readers, rejects live previews, and drains already registered status scans.</summary>
    /// <param name="projectRoot">The owning project directory.</param>
    /// <param name="cancellationToken">Cancels before any output replacement starts.</param>
    /// <returns>The exclusive publication lease.</returns>
    internal static async Task<CookOutputWriteLease> AcquireWriteAsync(string projectRoot, CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        var (root, gatePath, readers) = PreparePaths(projectRoot);
        var writer = new CookOutputWriteLease(root, readers, OpenGate(gatePath));
        try
        {
            RemoveReleasedReaders(readers);
            while (true)
            {
                cancellationToken.ThrowIfCancellationRequested();
                try
                {
                    RemoveReleasedReaders(readers, "*.scan");
                    break;
                }
                catch (CookOutputBusyException)
                {
                    await Task.Delay(50, cancellationToken).ConfigureAwait(false);
                }
            }

            return writer;
        }
        catch
        {
            writer.Dispose();
            throw;
        }
    }
}
