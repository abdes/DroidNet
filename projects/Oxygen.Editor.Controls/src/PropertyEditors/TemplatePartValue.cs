// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.UI.Xaml;

namespace Oxygen.Editor.Controls;

/// <summary>Keeps a value of a templated control and the same value of one of its template parts equal, both ways.</summary>
/// <remarks>
/// A TwoWay <c>{Binding}</c> to the templated parent is lost as soon as the part sets its own value, as an editing part
/// does on every edit; the control then stops refreshing the part, for example when a view model rejects an edit. This
/// sync survives the part's own writes: the control's value is pushed to the part whenever it changes, and the part's
/// value is taken back whenever the part changes it.
/// </remarks>
internal sealed class TemplatePartValue
{
    private readonly DependencyObject owner;
    private readonly DependencyProperty ownerProperty;
    private readonly DependencyProperty partProperty;
    private DependencyObject? part;
    private long callback;
    private bool syncing;

    /// <summary>Initializes a new instance of the <see cref="TemplatePartValue"/> class.</summary>
    /// <param name="owner">The templated control.</param>
    /// <param name="ownerProperty">The control's property.</param>
    /// <param name="partProperty">The part's property it mirrors.</param>
    public TemplatePartValue(DependencyObject owner, DependencyProperty ownerProperty, DependencyProperty partProperty)
    {
        this.owner = owner;
        this.ownerProperty = ownerProperty;
        this.partProperty = partProperty;
    }

    /// <summary>Attaches to a newly applied part, detaching from the previous one, and gives it the control's value.</summary>
    /// <param name="newPart">The part, or <see langword="null"/> when the template has none.</param>
    public void Attach(DependencyObject? newPart)
    {
        if (this.part is not null)
        {
            this.part.UnregisterPropertyChangedCallback(this.partProperty, this.callback);
        }

        this.part = newPart;
        if (newPart is not null)
        {
            this.callback = newPart.RegisterPropertyChangedCallback(this.partProperty, this.OnPartChanged);
            this.Push();
        }
    }

    /// <summary>Gives the part the control's value; called when the control's value changes.</summary>
    public void Push()
    {
        if (this.part is null || this.syncing)
        {
            return;
        }

        this.syncing = true;
        try
        {
            this.part.SetValue(this.partProperty, this.owner.GetValue(this.ownerProperty));
        }
        finally
        {
            this.syncing = false;
        }
    }

    private void OnPartChanged(DependencyObject sender, DependencyProperty property)
    {
        if (this.syncing)
        {
            return;
        }

        this.syncing = true;
        try
        {
            this.owner.SetValue(this.ownerProperty, sender.GetValue(property));
        }
        finally
        {
            this.syncing = false;
        }

        // A binding of the control may refuse the value at once and set its own; the part then shows that one.
        if (!Equals(this.owner.GetValue(this.ownerProperty), sender.GetValue(property)))
        {
            this.Push();
        }
    }
}
