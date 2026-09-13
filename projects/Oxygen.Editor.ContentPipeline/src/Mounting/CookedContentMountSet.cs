// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ContentPipeline.Mounting;

/// <summary>Transfers an ordered, validated set of cooked roots and its readers to the runtime.</summary>
public sealed partial class CookedContentMountSet : IDisposable
{
    private readonly IReadOnlyList<IDisposable> readers;
    private int disposed;

    /// <summary>Initializes a new instance of the <see cref="CookedContentMountSet"/> class.</summary>
    /// <param name="roots">The accepted native mount order.</param>
    /// <param name="readers">Readers transferred from successful preparation.</param>
    internal CookedContentMountSet(IReadOnlyList<string> roots, IReadOnlyList<IDisposable> readers)
    {
        this.Roots = roots;
        this.readers = readers;
    }

    /// <summary>Gets native mount order, with the highest-priority source last.</summary>
    public IReadOnlyList<string> Roots { get; }

    /// <inheritdoc />
    public void Dispose()
    {
        if (Interlocked.Exchange(ref this.disposed, 1) != 0)
        {
            return;
        }

        foreach (var reader in this.readers.Reverse())
        {
            reader.Dispose();
        }
    }
}
