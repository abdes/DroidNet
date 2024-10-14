// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Docking.Mocks;

using System.Diagnostics.CodeAnalysis;

/// <summary>
/// A dummy implementation of <see cref="IDocker" /> for testing.
/// </summary>
[ExcludeFromCodeCoverage]
public partial class DummyDocker : IDocker
{
#pragma warning disable CS0067 // Event is never used
    public event EventHandler<LayoutChangedEventArgs>? LayoutChanged;

#pragma warning restore CS0067 // Event is never used

    public void Dispose() => GC.SuppressFinalize(this);

    public void Dock(IDock dock, Anchor anchor, bool minimized = false) => throw new NotSupportedException();

    public void DumpWorkspace() => throw new NotSupportedException();

    public void FloatDock(IDock dock) => throw new NotSupportedException();

    public void Layout(LayoutEngine layoutEngine) => throw new NotSupportedException();

    public void MinimizeDock(IDock dock) => throw new NotSupportedException();

    public void PinDock(IDock dock) => throw new NotSupportedException();

    public void ResizeDock(IDock dock, Width? width, Height? height) => throw new NotSupportedException();

    public void CloseDock(IDock dock) => throw new NotSupportedException();
}
