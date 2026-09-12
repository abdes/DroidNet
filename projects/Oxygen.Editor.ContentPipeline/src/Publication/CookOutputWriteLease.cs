// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ContentPipeline.Publication;

/// <summary>Owns publication and registers new runtime readers before releasing the writer gate.</summary>
public sealed partial class CookOutputWriteLease : IDisposable
{
    private readonly Lock sync = new();
    private readonly string readersDirectory;
    private FileStream? gate;

    /// <summary>Initializes a new instance of the <see cref="CookOutputWriteLease"/> class, taking ownership of its gate.</summary>
    /// <param name="projectRoot">The normalized project directory.</param>
    /// <param name="readersDirectory">The registered reader markers.</param>
    /// <param name="gate">The already acquired publication gate.</param>
    internal CookOutputWriteLease(string projectRoot, string readersDirectory, FileStream gate)
    {
        this.ProjectRoot = projectRoot;
        this.readersDirectory = readersDirectory;
        this.gate = gate;
    }

    /// <summary>Gets the project whose published paths this lease protects.</summary>
    public string ProjectRoot { get; }

    /// <summary>Hands read ownership to a runtime while no competing publisher can enter.</summary>
    /// <returns>A reader lease that remains held after this publication lease is disposed.</returns>
    public IDisposable CreateReader()
    {
        lock (this.sync)
        {
            ObjectDisposedException.ThrowIf(this.gate is null, this);
            return CookOutputLease.CreateReader(this.readersDirectory);
        }
    }

    /// <inheritdoc />
    public void Dispose()
    {
        lock (this.sync)
        {
            this.gate?.Dispose();
            this.gate = null;
        }
    }

    /// <summary>Verifies that an operation still holds this project's write gate.</summary>
    /// <param name="projectRoot">The project expected by the operation.</param>
    internal void VerifyOwner(string projectRoot)
    {
        lock (this.sync)
        {
            ObjectDisposedException.ThrowIf(this.gate is null, this);
            if (!string.Equals(this.ProjectRoot, Path.GetFullPath(projectRoot), StringComparison.OrdinalIgnoreCase))
            {
                throw new InvalidOperationException("This writer belongs to another project.");
            }
        }
    }
}
