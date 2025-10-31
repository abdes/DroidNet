// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

#pragma warning disable SA1402 // File may only contain a single type
#pragma warning disable SA1649 // File name should match first type name

namespace DroidNet.Controls;

/// <summary>
///     Input data for a single item in the layout computation.
/// </summary>
public record LayoutPerItemInput(
    int Index,
    bool IsPinned,
    double MinWidth,
    double DesiredWidth);

/// <summary>
///     Request to compute the layout for a set of items.
/// </summary>
public record LayoutRequest(
    double AvailableWidth,
    IReadOnlyList<LayoutPerItemInput> Items);

/// <summary>
///     Output data for a single item after layout computation.
/// </summary>
public record LayoutPerItemOutput(
    int Index,
    double Width,
    bool IsPinned);

/// <summary>
///     Result of the layout computation.
/// </summary>
public record LayoutResult(
    IReadOnlyList<LayoutPerItemOutput> Items,
    double SumPinnedWidth,
    double SumRegularWidth,
    bool NeedsScrolling,
    string? Reason = null);
