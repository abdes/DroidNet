// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.ComponentModel;
using System.Runtime.CompilerServices;
using DroidNet.Routing;
using Oxygen.Editor.WorldEditor.Routing;

namespace Oxygen.Editor.WorldEditor.ContentBrowser;

public partial class LocalRouterContext(object targetObject)
    : NavigationContext(Target.Self, targetObject), ILocalRouterContext, INotifyPropertyChanged
{
    private readonly object? rootViewModel;
    private IRouter? router;
    private IRouter? globalRouter;

    /// <inheritdoc/>
    public event PropertyChangedEventHandler? PropertyChanged;

    /// <inheritdoc/>
    public object RootViewModel
    {
        get => this.rootViewModel ?? throw new InvalidOperationException();
        internal init => _ = this.SetField(ref this.rootViewModel, value);
    }

    /// <inheritdoc/>
    public IRouter LocalRouter
    {
        get => this.router ?? throw new InvalidOperationException();
        internal set => _ = this.SetField(ref this.router, value);
    }

    /// <inheritdoc/>
    public IRouter ParentRouter
    {
        get => this.globalRouter ?? throw new InvalidOperationException();
        internal set => _ = this.SetField(ref this.globalRouter, value);
    }

    private void OnPropertyChanged([CallerMemberName] string? propertyName = null)
        => this.PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(propertyName));

    private bool SetField<T>(ref T field, T value, [CallerMemberName] string? propertyName = null)
    {
        if (EqualityComparer<T>.Default.Equals(field, value))
        {
            return false;
        }

        field = value;
        this.OnPropertyChanged(propertyName);
        return true;
    }
}
