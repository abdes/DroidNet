// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Windows.Input;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Oxygen.Editor.World.Inspector.Geometry;

namespace Oxygen.Editor.World.Inspector.Controls;

/// <summary>Shares geometry and material catalog presentation without owning assignment or catalog services.</summary>
public sealed partial class AssetPickerContent : UserControl
{
    /// <summary>Identifies the existing stable catalog groups.</summary>
    public static readonly DependencyProperty GroupsProperty = Register<object>(nameof(Groups), null);

    /// <summary>Identifies the typed material rather than geometry row template.</summary>
    public static readonly DependencyProperty IsMaterialProperty = Register<bool>(nameof(IsMaterial), false, OnKindChanged);

    /// <summary>Identifies whether the owning catalog has a notice.</summary>
    public static readonly DependencyProperty HasNoticeProperty = Register<bool>(nameof(HasNotice), false);

    /// <summary>Identifies the owning catalog's notice.</summary>
    public static readonly DependencyProperty NoticeTextProperty = Register<string>(nameof(NoticeText), string.Empty);

    /// <summary>Identifies the owning catalog's retry action.</summary>
    public static readonly DependencyProperty RetryCommandProperty = Register<ICommand>(nameof(RetryCommand), null);

    /// <summary>Initializes a new instance of the <see cref="AssetPickerContent"/> class.</summary>
    public AssetPickerContent()
    {
        this.InitializeComponent();
        this.UpdateTemplate();
    }

    /// <summary>Reports the original stable row so the owner captures the assignment target.</summary>
    public event EventHandler<AssetPickerItemInvokedEventArgs>? ItemClicked;

    /// <summary>Gets or sets existing stable groups; no rows are projected or replaced here.</summary>
    public object? Groups { get => this.GetValue(GroupsProperty); set => this.SetValue(GroupsProperty, value); }

    /// <summary>Gets or sets a value indicating whether material templates are used.</summary>
    public bool IsMaterial { get => (bool)this.GetValue(IsMaterialProperty); set => this.SetValue(IsMaterialProperty, value); }

    /// <summary>Gets or sets a value indicating whether the catalog notice is shown.</summary>
    public bool HasNotice { get => (bool)this.GetValue(HasNoticeProperty); set => this.SetValue(HasNoticeProperty, value); }

    /// <summary>Gets or sets the owning catalog's notice.</summary>
    public string NoticeText { get => (string)this.GetValue(NoticeTextProperty); set => this.SetValue(NoticeTextProperty, value); }

    /// <summary>Gets or sets the owning catalog's retry action.</summary>
    public ICommand? RetryCommand { get => (ICommand?)this.GetValue(RetryCommandProperty); set => this.SetValue(RetryCommandProperty, value); }

    private static DependencyProperty Register<T>(string name, object? defaultValue, PropertyChangedCallback? changed = null)
        => DependencyProperty.Register(name, typeof(T), typeof(AssetPickerContent), new PropertyMetadata(defaultValue, changed));

    private static void OnKindChanged(DependencyObject sender, DependencyPropertyChangedEventArgs args)
        => ((AssetPickerContent)sender).UpdateTemplate();

    private void UpdateTemplate()
    {
        if (this.GroupList is { } list)
        {
            list.ItemTemplate = (DataTemplate)this.Resources[this.IsMaterial ? "MaterialGroupTemplate" : "GeometryGroupTemplate"];
        }
    }

    private void OnItemClicked(object sender, RoutedEventArgs args)
    {
        if (sender is FrameworkElement { DataContext: AssetPickerRow asset })
        {
            this.ItemClicked?.Invoke(this, new(asset));
        }
        else if (sender is FrameworkElement { DataContext: MaterialPickerRow material })
        {
            this.ItemClicked?.Invoke(this, new(material));
        }
    }
}
