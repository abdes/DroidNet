// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Docking;

public interface IDocker : IDisposable
{
    event EventHandler<LayoutChangedEventArgs> LayoutChanged;

    void CloseDock(IDock dock);

    void Dock(IDock dock, Anchor anchor, bool minimized = false);

    void FloatDock(IDock dock);

    void MinimizeDock(IDock dock);

    void PinDock(IDock dock);

    void ResizeDock(IDock dock, Width? width, Height? height);

    void DumpWorkspace();

    void Layout(LayoutEngine layoutEngine);
}

public class LayoutChangedEventArgs(LayoutChangeReason reason) : EventArgs
{
    public LayoutChangeReason Reason => reason;
}
