// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text;

namespace Oxygen.Editor.ContentPipeline.Processes;

/// <summary>Drains native output without cancelling readers before the owned process exits.</summary>
internal sealed partial class WindowsContentPipelineWorker
{
    private static async Task<string> ReadOutputAsync(StreamReader reader, bool isStandardError, IProgress<ContentPipelineProcessOutput>? output)
    {
        var captured = new StringBuilder();
        var line = new StringBuilder();
        var buffer = new char[4096];
        int count;
        while ((count = await reader.ReadAsync(buffer.AsMemory(), CancellationToken.None).ConfigureAwait(false)) != 0)
        {
            _ = captured.Append(buffer, 0, count);
            if (output is null)
            {
                continue;
            }

            for (var index = 0; index < count; index++)
            {
                var character = buffer[index];
                if (character is '\r' or '\n')
                {
                    if (line.Length != 0)
                    {
                        output.Report(new(line.ToString(), isStandardError));
                        _ = line.Clear();
                    }
                }
                else
                {
                    _ = line.Append(character);
                }
            }
        }

        if (line.Length != 0)
        {
            output?.Report(new(line.ToString(), isStandardError));
        }

        return captured.ToString();
    }
}
