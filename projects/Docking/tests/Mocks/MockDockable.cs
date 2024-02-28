// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Docking.Mocks;

using System.Diagnostics.CodeAnalysis;

[ExcludeFromCodeCoverage]
public class MockDockable(string id) : IDockable
{
    public event Action? OnDisposed;

    public string Id => id;

    public string Title => this.Id;

    public string MinimizedTitle => this.Id;

    public string TabbedTitle => this.Id;

    public IDockable.Width PreferredWidth { get; set; } = new();

    public IDockable.Height PreferredHeight { get; set; } = new();

    public object? ViewModel => null;

    public IDock? Owner { get; set; }

    public bool IsActive { get; set; }

    public void Dispose()
    {
        this.OnDisposed?.Invoke();
        GC.SuppressFinalize(this);
    }

    public override string? ToString() => this.Id;
}
