// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;

namespace DroidNet.Docking.Tests.Mocks;

/// <summary>
/// A dummy implementation of <see cref="IDocker" /> for testing.
/// </summary>
[ExcludeFromCodeCoverage]
public partial class DummyDocker : IDocker
{
    private bool isDisposed;

#pragma warning disable CS0067 // Event is never used
    /// <inheritdoc/>
    public event EventHandler<LayoutChangedEventArgs>? LayoutChanged;

#pragma warning restore CS0067 // Event is never used

    /// <inheritdoc/>
    public void Dispose()
    {
        this.Dispose(disposing: true);
        GC.SuppressFinalize(this);
    }

    /// <inheritdoc/>
    public void Dock(IDock dock, Anchor anchor, bool minimized = false) => throw new NotSupportedException();

    /// <inheritdoc/>
    public void DumpWorkspace() => throw new NotSupportedException();

    /// <inheritdoc/>
    public void FloatDock(IDock dock) => throw new NotSupportedException();

    /// <inheritdoc/>
    public void Layout(LayoutEngine layoutEngine) => throw new NotSupportedException();

    /// <inheritdoc/>
    public void MinimizeDock(IDock dock) => throw new NotSupportedException();

    /// <inheritdoc/>
    public void PinDock(IDock dock) => throw new NotSupportedException();

    /// <inheritdoc/>
    public void ResizeDock(IDock dock, Width? width, Height? height) => throw new NotSupportedException();

    /// <inheritdoc/>
    public void CloseDock(IDock dock) => throw new NotSupportedException();

    protected virtual void Dispose(bool disposing)
    {
        if (this.isDisposed)
        {
            return;
        }

        if (disposing)
        {
            /* Dispose of managed resources */
        }

        /* Dispose of unmanaged resources */
        this.isDisposed = true;
    }
}
