// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ContentPipeline.Publication;

/// <summary>Owns the project selection gate through commit or rollback.</summary>
internal sealed class CookOutputWriteLease : IDisposable
{
    private readonly Lock sync = new();
    private FileStream? gate;

    /// <summary>Initializes a new instance of the <see cref="CookOutputWriteLease"/> class, taking ownership of its gate.</summary>
    /// <param name="projectRoot">The normalized project directory.</param>
    /// <param name="gate">The already acquired publication gate.</param>
    internal CookOutputWriteLease(string projectRoot, FileStream gate)
    {
        this.ProjectRoot = projectRoot;
        this.gate = gate;
    }

    /// <summary>Gets the project whose published paths this lease protects.</summary>
    public string ProjectRoot { get; }

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
