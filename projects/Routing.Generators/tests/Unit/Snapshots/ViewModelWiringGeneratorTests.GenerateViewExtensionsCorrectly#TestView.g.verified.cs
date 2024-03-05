//HintName: TestView.g.cs
// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Testing;

using Testing;
using DroidNet.Routing.View;
using Microsoft.UI.Xaml;

#nullable enable
/// <summary>
/// An empty page that can be used on its own or navigated to within a Frame.
/// </summary>
public partial class TestView : IViewFor<TestViewModel>
{
    /// <summary>
    /// A dependency property for the `ViewModel` associated with this `View`.
    /// </summary>
    /// <remarks>
    /// Its initial value comes from the DI service provider. It can subsequently
    /// be set to a different value and you can listen on the <see cref="ViewModelChanged" />
    /// event to be notified of such changes.
    /// </remarks>
    public static readonly DependencyProperty ViewModelProperty = DependencyProperty.Register(
        nameof(TestViewModel),
        typeof(TestViewModel),
        typeof(TestView),
        new PropertyMetadata(
            null,
            (d, e) => ((TestView)d).ViewModelChanged?.Invoke((IViewFor)d, EventArgs.Empty)));

    /// <inheritdoc />
    public event EventHandler? ViewModelChanged;

    /// <inheritdoc />
    public TestViewModel ViewModel
    {
        get => (TestViewModel)this.GetValue(ViewModelProperty);
        set => this.SetValue(ViewModelProperty, value);
    }

    object IViewFor.ViewModel
    {
        get => this.ViewModel;
        set => this.SetValue(ViewModelProperty, value);
    }
}
#nullable disable
