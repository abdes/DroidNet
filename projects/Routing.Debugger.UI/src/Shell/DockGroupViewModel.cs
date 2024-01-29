// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing.Debugger.UI.Shell;

using System.Diagnostics;
using DroidNet.Docking;

public class DockGroupViewModel
{
    private readonly IDockGroup model1;

    public DockGroupViewModel(IDockGroup model)
    {
        this.model1 = model;
        this.Orientation = model.Orientation;
        this.First = model.First != null ? new DockGroupViewModel(model.First) : null;
        this.Second = model.Second != null ? new DockGroupViewModel(model.Second) : null;
        this.IsEmpty = model.IsEmpty;

        Debug.WriteLine($"New DockGroupViewModel: {model}");
    }

    public Orientation Orientation { get; }

    public DockGroupViewModel? First { get; }

    public DockGroupViewModel? Second { get; }

    public bool IsEmpty { get; }

    public override string? ToString() => this.model1.ToString();
}
