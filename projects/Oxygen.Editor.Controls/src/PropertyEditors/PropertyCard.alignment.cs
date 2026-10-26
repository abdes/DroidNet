// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Media;

namespace Oxygen.Editor.Controls;

/// <summary>Lines up the editors of rows that share a <see cref="QualifierGroup"/>.</summary>
/// <remarks>
///     A group lives in its scope: the <see cref="PropertiesExpander"/> that shows the row, or the root of the row's
///     visual tree when no section owns it. Rows of different sections, and rows a recycling items host keeps loaded
///     outside the shown section, never widen each other's group.
/// </remarks>
public partial class PropertyCard
{
    // Holds a scope's groups on the scope element itself, so they live and go with it.
    private static readonly DependencyProperty QualifierGroupsProperty = DependencyProperty.RegisterAttached(
        "QualifierGroups", typeof(object), typeof(PropertyCard), new PropertyMetadata(defaultValue: null));

    private Dictionary<PropertyCard, double>? qualifierGroup;

    private static void OnQualifierGroupChanged(DependencyObject sender, DependencyPropertyChangedEventArgs args)
    {
        var card = (PropertyCard)sender;
        card.LeaveQualifierGroup();
        card.InvalidateMeasure();
    }

    private static double Widest(Dictionary<PropertyCard, double> members) => members.Values.DefaultIfEmpty(0).Max();

    // Records this row's trailing width and returns the widest in its group; the row reserves the difference.
    private double AlignWithQualifierGroup(double trailingWidth)
    {
        if (string.IsNullOrEmpty(this.QualifierGroup) || !this.IsLoaded || this.Visibility != Visibility.Visible)
        {
            this.LeaveQualifierGroup();
            return trailingWidth;
        }

        this.qualifierGroup ??= this.JoinQualifierGroup();
        var before = Widest(this.qualifierGroup);
        this.qualifierGroup[this] = trailingWidth;
        var widest = Widest(this.qualifierGroup);
        this.RemeasureGroupIfChanged(this.qualifierGroup, before, widest);
        return widest;
    }

    // Finds the members of this row's group in its scope, creating the group on first use.
    private Dictionary<PropertyCard, double> JoinQualifierGroup()
    {
        var scope = this.FindQualifierGroupScope();
        if (scope.GetValue(QualifierGroupsProperty) is not ScopeGroups groups)
        {
            groups = new ScopeGroups();
            scope.SetValue(QualifierGroupsProperty, groups);
        }

        if (!groups.ByName.TryGetValue(this.QualifierGroup, out var members))
        {
            members = [];
            groups.ByName[this.QualifierGroup] = members;
        }

        return members;
    }

    // The nearest section above the row, or the root of its visual tree outside one.
    private DependencyObject FindQualifierGroupScope()
    {
        DependencyObject scope = this;
        for (var ancestor = VisualTreeHelper.GetParent(this); ancestor is not null; ancestor = VisualTreeHelper.GetParent(ancestor))
        {
            if (ancestor is PropertiesExpander)
            {
                return ancestor;
            }

            scope = ancestor;
        }

        return scope;
    }

    private void LeaveQualifierGroup()
    {
        if (this.qualifierGroup is not { } members)
        {
            return;
        }

        this.qualifierGroup = null;
        var before = Widest(members);
        _ = members.Remove(this);
        this.RemeasureGroupIfChanged(members, before, Widest(members));
    }

    private void RemeasureGroupIfChanged(Dictionary<PropertyCard, double> members, double before, double after)
    {
        if (Math.Abs(after - before) < 0.01)
        {
            return;
        }

        foreach (var member in members.Keys.Where(member => !ReferenceEquals(member, this)))
        {
            member.InvalidateMeasure();
        }
    }

    private void OnVisibilityChanged()
    {
        if (this.Visibility != Visibility.Visible)
        {
            this.LeaveQualifierGroup();
        }

        this.InvalidateMeasure();
    }

    // The groups of one scope, by name, each with the trailing annotation width every loaded, visible member last measured.
    private sealed partial class ScopeGroups
    {
        public Dictionary<string, Dictionary<PropertyCard, double>> ByName { get; } = [];
    }
}
