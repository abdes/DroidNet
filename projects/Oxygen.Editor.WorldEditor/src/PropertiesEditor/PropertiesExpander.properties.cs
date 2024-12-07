// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;

namespace Oxygen.Editor.WorldEditor.PropertiesEditor;

public partial class PropertiesExpander
{
    /// <summary>
    /// The backing <see cref="DependencyProperty"/> for the <see cref="Header"/> property.
    /// </summary>
    public static readonly DependencyProperty HeaderProperty = DependencyProperty.Register(
        nameof(Header),
        typeof(object),
        typeof(PropertiesExpander),
        new PropertyMetadata(defaultValue: null, (d, e) => ((PropertiesExpander)d).OnHeaderPropertyChanged(e.OldValue, e.NewValue)));

    /// <summary>
    /// The backing <see cref="DependencyProperty"/> for the <see cref="Description"/> property.
    /// </summary>
    public static readonly DependencyProperty DescriptionProperty = DependencyProperty.Register(
        nameof(Description),
        typeof(object),
        typeof(PropertiesExpander),
        new PropertyMetadata(defaultValue: null, (d, e) => ((PropertiesExpander)d).OnDescriptionPropertyChanged(e.OldValue, e.NewValue)));

    /// <summary>
    /// The backing <see cref="DependencyProperty"/> for the <see cref="HeaderIcon"/> property.
    /// </summary>
    public static readonly DependencyProperty HeaderIconProperty = DependencyProperty.Register(
        nameof(HeaderIcon),
        typeof(IconElement),
        typeof(PropertiesExpander),
        new PropertyMetadata(defaultValue: null, (d, e) => ((PropertiesExpander)d).OnHeaderIconPropertyChanged((IconElement)e.OldValue, (IconElement)e.NewValue)));

    /// <summary>
    /// The backing <see cref="DependencyProperty"/> for the <see cref="Content"/> property.
    /// </summary>
    public static readonly DependencyProperty ContentProperty = DependencyProperty.Register(
        nameof(Content),
        typeof(object),
        typeof(PropertiesExpander),
        new PropertyMetadata(defaultValue: null));

    /// <summary>
    /// The backing <see cref="DependencyProperty"/> for the <see cref="IsExpanded"/> property.
    /// </summary>
    public static readonly DependencyProperty IsExpandedProperty = DependencyProperty.Register(
        nameof(IsExpanded),
        typeof(bool),
        typeof(PropertiesExpander),
        new PropertyMetadata(defaultValue: false, (d, e) => ((PropertiesExpander)d).OnIsExpandedPropertyChanged((bool)e.OldValue, (bool)e.NewValue)));

    /// <summary>
    /// The backing <see cref="DependencyProperty"/> for the <see cref="ItemsHeader"/> property.
    /// </summary>
    public static readonly DependencyProperty ItemsHeaderProperty = DependencyProperty.Register(
        nameof(ItemsHeader),
        typeof(UIElement),
        typeof(PropertiesExpander),
        new PropertyMetadata(defaultValue: null));

    /// <summary>
    /// The backing <see cref="DependencyProperty"/> for the <see cref="ItemsFooter"/> property.
    /// </summary>
    public static readonly DependencyProperty ItemsFooterProperty = DependencyProperty.Register(
        nameof(ItemsFooter),
        typeof(UIElement),
        typeof(PropertiesExpander),
        new PropertyMetadata(defaultValue: null));

    public static readonly DependencyProperty ItemsProperty =
        DependencyProperty.Register(nameof(Items), typeof(IList<object>), typeof(PropertiesExpander), new PropertyMetadata(null));

    ///// <summary>
    ///// The backing <see cref="DependencyProperty"/> for the <see cref="ActionIcon"/> property.
    ///// </summary>
    // public static readonly DependencyProperty ActionIconProperty = DependencyProperty.Register(
    //    nameof(ActionIcon),
    //    typeof(IconElement),
    //    typeof(PropertiesExpander),
    //    new PropertyMetadata(defaultValue: "\ue974"));

    ///// <summary>
    ///// The backing <see cref="DependencyProperty"/> for the <see cref="ActionIconToolTip"/> property.
    ///// </summary>
    // public static readonly DependencyProperty ActionIconToolTipProperty = DependencyProperty.Register(
    //    nameof(ActionIconToolTip),
    //    typeof(string),
    //    typeof(PropertiesExpander),
    //    new PropertyMetadata(defaultValue: null));

    ///// <summary>
    ///// The backing <see cref="DependencyProperty"/> for the <see cref="IsActionIconVisible"/> property.
    ///// </summary>
    // public static readonly DependencyProperty IsActionIconVisibleProperty = DependencyProperty.Register(
    //    nameof(IsActionIconVisible),
    //    typeof(bool),
    //    typeof(PropertiesExpander),
    //    new PropertyMetadata(defaultValue: true, (d, e) => ((PropertiesExpander)d).OnIsActionIconVisiblePropertyChanged((bool)e.OldValue, (bool)e.NewValue)));

    ///// <summary>
    ///// The backing <see cref="DependencyProperty"/> for the <see cref="IsClickEnabled"/> property.
    ///// </summary>
    // public static readonly DependencyProperty IsClickEnabledProperty = DependencyProperty.Register(
    //    nameof(IsClickEnabled),
    //    typeof(bool),
    //    typeof(PropertiesExpander),
    //    new PropertyMetadata(defaultValue: false, (d, e) => ((PropertiesExpander)d).OnIsClickEnabledPropertyChanged((bool)e.OldValue, (bool)e.NewValue)));

    /// <summary>
    /// Gets or sets the Header.
    /// </summary>
    public object Header
    {
        get => this.GetValue(HeaderProperty);
        set => this.SetValue(HeaderProperty, value);
    }

    /// <summary>
    /// Gets or sets the Content.
    /// </summary>
    public object Content
    {
        get => this.GetValue(ContentProperty);
        set => this.SetValue(ContentProperty, value);
    }

    /// <summary>
    /// Gets or sets the ItemsFooter.
    /// </summary>
    public UIElement ItemsHeader
    {
        get => (UIElement)this.GetValue(ItemsHeaderProperty);
        set => this.SetValue(ItemsHeaderProperty, value);
    }

    /// <summary>
    /// Gets or sets the ItemsFooter.
    /// </summary>
    public UIElement ItemsFooter
    {
        get => (UIElement)this.GetValue(ItemsFooterProperty);
        set => this.SetValue(ItemsFooterProperty, value);
    }

    public IList<object> Items
    {
        get => (IList<object>)this.GetValue(ItemsProperty);
        set => this.SetValue(ItemsProperty, value);
    }

    /// <summary>
    /// Gets or sets the Description.
    /// </summary>
    public object Description
    {
        get => this.GetValue(DescriptionProperty);
        set => this.SetValue(DescriptionProperty, value);
    }

    /// <summary>
    /// Gets or sets the HeaderIcon.
    /// </summary>
    public IconElement? HeaderIcon
    {
        get => (IconElement)this.GetValue(HeaderIconProperty);
        set => this.SetValue(HeaderIconProperty, value);
    }

    /// <summary>
    /// Gets or sets a value indicating whether gets or sets the IsExpanded state.
    /// </summary>
    public bool IsExpanded
    {
        get => (bool)this.GetValue(IsExpandedProperty);
        set => this.SetValue(IsExpandedProperty, value);
    }

    ///// <summary>
    ///// Gets or sets the icon that is shown when IsClickEnabled is set to true.
    ///// </summary>
    // public IconElement ActionIcon
    // {
    //    get => (IconElement)this.GetValue(ActionIconProperty);
    //    set => this.SetValue(ActionIconProperty, value);
    // }

    ///// <summary>
    ///// Gets or sets the tooltip of the ActionIcon.
    ///// </summary>
    // public string ActionIconToolTip
    // {
    //    get => (string)this.GetValue(ActionIconToolTipProperty);
    //    set => this.SetValue(ActionIconToolTipProperty, value);
    // }

    ///// <summary>
    ///// Gets or sets a value indicating whether gets or sets if the ActionIcon is shown.
    ///// </summary>
    // public bool IsActionIconVisible
    // {
    //    get => (bool)this.GetValue(IsActionIconVisibleProperty);
    //    set => this.SetValue(IsActionIconVisibleProperty, value);
    // }

    ///// <summary>
    ///// Gets or sets a value indicating whether gets or sets if the card can be clicked.
    ///// </summary>
    // public bool IsClickEnabled
    // {
    //    get => (bool)this.GetValue(IsClickEnabledProperty);
    //    set => this.SetValue(IsClickEnabledProperty, value);
    // }
    protected virtual void OnIsExpandedPropertyChanged(bool oldValue, bool newValue)
    {
        if (newValue)
        {
            this.Expanded?.Invoke(this, EventArgs.Empty);
        }
        else
        {
            this.Collapsed?.Invoke(this, EventArgs.Empty);
        }
    }

    // protected virtual void OnIsClickEnabledPropertyChanged(bool oldValue, bool newValue) => this.OnIsClickEnabledChanged();
    protected virtual void OnHeaderIconPropertyChanged(IconElement oldValue, IconElement newValue) => this.OnHeaderIconChanged();

    protected virtual void OnHeaderPropertyChanged(object oldValue, object newValue) => this.OnHeaderChanged();

    protected virtual void OnDescriptionPropertyChanged(object oldValue, object newValue) => this.OnDescriptionChanged();

    // protected virtual void OnIsActionIconVisiblePropertyChanged(bool oldValue, bool newValue) => this.OnActionIconChanged();
}
