// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using System.Globalization;
using System.Text;
using System.Text.Json;

namespace Oxygen.Editor.ContentPipeline.WorkerProbe;

/// <summary>Controlled process and descendant writer for worker-lifetime tests.</summary>
internal static class Program
{
    private static async Task<int> Main(string[] args)
    {
        Console.OutputEncoding = Encoding.UTF8;
        if (args.Length == 0)
        {
            return 2;
        }

        if (string.Equals(args[0], "echo", StringComparison.Ordinal))
        {
            await Console.Out.WriteAsync(JsonSerializer.Serialize(args[1..])).ConfigureAwait(false);
            return 0;
        }

        if (string.Equals(args[0], "flood", StringComparison.Ordinal))
        {
            var output = new string('x', 300_000);
            var error = new string('y', 300_000);
            await Task.WhenAll(Console.Out.WriteAsync(output), Console.Error.WriteAsync(error)).ConfigureAwait(false);
            return 7;
        }

        if (string.Equals(args[0], "exit", StringComparison.Ordinal))
        {
            return int.Parse(args[1], CultureInfo.InvariantCulture);
        }

        if (string.Equals(args[0], "hold-file", StringComparison.Ordinal))
        {
            return await HoldFileAsync(args[1]).ConfigureAwait(false);
        }

        var root = args[1];
        var name = args.Length > 2 ? args[2] : "root";
        if (args[0] is "tree" or "orphan")
        {
            var start = new ProcessStartInfo(Environment.ProcessPath!)
            {
                UseShellExecute = false,
                CreateNoWindow = true,
            };
            start.ArgumentList.Add("write");
            start.ArgumentList.Add(root);
            start.ArgumentList.Add("child");
            using var child = Process.Start(start)!;
            if (string.Equals(args[0], "orphan", StringComparison.Ordinal))
            {
                return 0;
            }
        }

        await File.WriteAllTextAsync(Path.Combine(root, name + ".pid"), Environment.ProcessId.ToString(CultureInfo.InvariantCulture)).ConfigureAwait(false);
        var outputPath = Path.Combine(root, name + ".writes");
        while (true)
        {
            await File.AppendAllTextAsync(outputPath, "write\n").ConfigureAwait(false);
            await Console.Out.WriteLineAsync(name).ConfigureAwait(false);
            await Console.Error.WriteLineAsync(name).ConfigureAwait(false);
            await Task.Delay(10).ConfigureAwait(false);
        }
    }

    private static async Task<int> HoldFileAsync(string path)
    {
        var held = new FileStream(path, FileMode.CreateNew, FileAccess.ReadWrite, FileShare.None);
        await using var lifetime = held.ConfigureAwait(false);
        await Console.Out.WriteLineAsync("ready").ConfigureAwait(false);
        await Console.Out.FlushAsync().ConfigureAwait(false);
        _ = await Console.In.ReadLineAsync().ConfigureAwait(false);
        return 0;
    }
}
