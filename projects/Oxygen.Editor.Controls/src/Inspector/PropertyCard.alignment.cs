// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Runtime.CompilerServices;
using Microsoft.UI.Xaml;

namespace Oxygen.Editor.Controls;

/// <summary>Lines up the editors of rows that share a <see cref="QualifierGroup"/>.</summary>
public partial class PropertyCard
{
    // Loaded, visible rows per window and group name, with the trailing annotation width each one last measured.
    private static readonly ConditionalWeakTable<XamlRoot, Dictionary<string, Dictionary<PropertyCard, double>>> QualifierGroups = [];

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
        if (string.IsNullOrEmpty(this.QualifierGroup) || !this.IsLoaded || this.XamlRoot is not { } root || this.Visibility != Visibility.Visible)
        {
            this.LeaveQualifierGroup();
            return trailingWidth;
        }

        if (this.qualifierGroup is null)
        {
            var groups = QualifierGroups.GetOrCreateValue(root);
            if (!groups.TryGetValue(this.QualifierGroup, out var members))
            {
                members = [];
                groups[this.QualifierGroup] = members;
            }

            this.qualifierGroup = members;
        }

        var before = Widest(this.qualifierGroup);
        this.qualifierGroup[this] = trailingWidth;
        var widest = Widest(this.qualifierGroup);
        this.RemeasureGroupIfChanged(this.qualifierGroup, before, widest);
        return widest;
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
}
