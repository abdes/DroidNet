// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Docking.Mocks;

using DroidNet.Docking.Detail;

public class DummyDocker : IDocker
{
    public DummyDocker() => this.Root = new RootDockGroup(this);

#pragma warning disable CS0067 // Event is never used
    public event Action<LayoutChangeReason>? LayoutChanged;
#pragma warning restore  CS0067 // Event is never used

    public IDockGroup Root { get; }

    public void Dispose() => GC.SuppressFinalize(this);

    public void CloseDock(IDock dock) => throw new NotImplementedException();

    public void Dock(IDock dock, Anchor anchor, bool minimized = false) => throw new NotImplementedException();

    public void DockToCenter(IDock dock) => throw new NotImplementedException();

    public void DockToRoot(IDock dock, AnchorPosition position, bool minimized = false)
        => throw new NotImplementedException();

    public void FloatDock(IDock dock) => throw new NotImplementedException();

    public void MinimizeDock(IDock dock) => throw new NotImplementedException();

    public void PinDock(IDock dock) => throw new NotImplementedException();

    public void ResizeDock(IDock dock, Width? width, Height? height) => throw new NotImplementedException();
}
