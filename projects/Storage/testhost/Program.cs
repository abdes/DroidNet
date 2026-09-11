// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Globalization;
using DroidNet.Storage.Native;
using Testably.Abstractions;

namespace DroidNet.Storage.AtomicWriteProbe;

/// <summary>Terminates only this test process at a deterministic atomic-save boundary.</summary>
internal static class Program
{
    private static async Task<int> Main(string[] args)
    {
        if (args.Length != 2 || !int.TryParse(args[1], CultureInfo.InvariantCulture, out var interruptedStage))
        {
            return 2;
        }

        var store = new NativeAtomicFileStore(new RealFileSystem(), async (stage, stream, token) =>
        {
            if ((int)stage != interruptedStage)
            {
                return;
            }

            if (stage == AtomicWriteStage.BeforeWrite)
            {
                await stream!.WriteAsync("{"u8.ToArray(), token).ConfigureAwait(false);
                await stream.FlushAsync(token).ConfigureAwait(false);
            }

            // Exit bypasses the writer's finally/catch cleanup, just as loss of the process does.
            Environment.Exit(97);
        });
        var baseline = await store.ReadAsync(args[0], CancellationToken.None).ConfigureAwait(false);
        _ = await store.WriteAsync(args[0], "new complete document"u8.ToArray(), baseline.Version, CancellationToken.None).ConfigureAwait(false);
        if (interruptedStage == 3)
        {
            Environment.Exit(97);
        }

        return 0;
    }
}
