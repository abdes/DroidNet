// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Projects;

/// <summary>Serializes writes to the same document path and releases unused coordination state.</summary>
public sealed partial class DocumentWriteCoordinator
{
    private readonly Lock sync = new();
    private readonly Dictionary<string, WriteQueue> queues = [with(StringComparer.OrdinalIgnoreCase)];

    /// <summary>Runs an operation after earlier writes to its destination have completed.</summary>
    /// <typeparam name="T">The operation result.</typeparam>
    /// <param name="path">The canonical document destination.</param>
    /// <param name="operation">Captures and writes the document after acquiring its queue.</param>
    /// <param name="cancellationToken">Cancels waiting for the queue.</param>
    /// <returns>The result of the serialized operation.</returns>
    public async Task<T> RunAsync<T>(string path, Func<Task<T>> operation, CancellationToken cancellationToken = default)
    {
        WriteQueue queue;
        lock (this.sync)
        {
            if (!this.queues.TryGetValue(path, out queue!))
            {
                queue = new WriteQueue();
                this.queues.Add(path, queue);
            }

            ++queue.Users;
        }

        var entered = false;
        try
        {
            await queue.Gate.WaitAsync(cancellationToken).ConfigureAwait(true);
            entered = true;
            return await operation().ConfigureAwait(true);
        }
        finally
        {
            lock (this.sync)
            {
                if (entered)
                {
                    _ = queue.Gate.Release();
                }

                if (--queue.Users == 0)
                {
                    _ = this.queues.Remove(path);
                    queue.Dispose();
                }
            }
        }
    }

    private sealed partial class WriteQueue : IDisposable
    {
        public SemaphoreSlim Gate { get; } = new(1, 1);

        public int Users { get; set; }

        public void Dispose() => this.Gate.Dispose();
    }
}
