// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;

namespace Oxygen.Editor.Core;

public static class MathUtil
{
    private const float Epsilon = 1e-5f;

    [SuppressMessage("ReSharper", "CompareOfFloatsByEqualityOperator", Justification = "comparing null values")]
    public static bool AreSame(float? a, float? b) => a == null || b == null
        ? a == b
        : MathF.Abs(a.Value - b.Value) < Epsilon;

    public static bool IsSameAs(this float a, float? b) => AreSame(a, b);
}
