// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.ComponentModel;
using System.Runtime.CompilerServices;
using DroidNet.Routing;
using Oxygen.Editor.World.Routing;

namespace Oxygen.Editor.World.ContentBrowser;

/// <summary>
/// Represents the router context for a local child router.
/// </summary>
/// <param name="target">The navigation target.</param>
/// <remarks>
/// The <see cref="LocalRouterContext"/> class extends the <see cref="NavigationContext"/> class and implements
/// the <see cref="ILocalRouterContext"/> and <see cref="INotifyPropertyChanged"/> interfaces. It provides additional
/// properties specific to the local routing context, including references to the root ViewModel, the local router,
/// and the parent router.
/// <para>
/// Local routing is always done in the same target then the parent navigation context. Therefore,
/// it will always use <see cref="Target.Self"/> as a target key. If you need to navigate to a
/// different target, you must use the global or parent router.
/// </para>
/// </remarks>
public partial class LocalRouterContext(object target)
    : NavigationContext(Target.Self, target), ILocalRouterContext, INotifyPropertyChanged
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

    /// <summary>
    /// Raises the <see cref="PropertyChanged"/> event.
    /// </summary>
    /// <param name="propertyName">The name of the property that changed.</param>
    private void OnPropertyChanged([CallerMemberName] string? propertyName = null)
        => this.PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(propertyName));

    /// <summary>
    /// Sets the field to the specified value and raises the <see cref="PropertyChanged"/> event if the value changes.
    /// </summary>
    /// <typeparam name="T">The type of the field.</typeparam>
    /// <param name="field">The field to set.</param>
    /// <param name="value">The value to set.</param>
    /// <param name="propertyName">The name of the property that changed.</param>
    /// <returns><see langword="true"/> if the value was changed; otherwise, <see langword="false"/>.</returns>
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
