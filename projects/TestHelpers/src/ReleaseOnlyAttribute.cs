// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.VisualStudio.TestTools.UnitTesting;

namespace DroidNet.TestHelpers;

/// <summary>
///     Runs a test class or method only in Release builds; in Debug builds MSTest reports it as skipped.
/// </summary>
/// <remarks>
///     Benchmarks measure optimized code, so a Debug run spends time and produces numbers that mean nothing.
/// </remarks>
[AttributeUsage(AttributeTargets.Class | AttributeTargets.Method, AllowMultiple = false)]
public sealed class ReleaseOnlyAttribute : ConditionBaseAttribute
{
#if DEBUG
    private const bool IsReleaseBuild = false;
#else
    private const bool IsReleaseBuild = true;
#endif

    /// <summary>
    ///     Initializes a new instance of the <see cref="ReleaseOnlyAttribute"/> class.
    /// </summary>
    public ReleaseOnlyAttribute()
        : base(ConditionMode.Include)
    {
        this.IgnoreMessage = "Runs only in Release builds.";
    }

    /// <inheritdoc/>
    public override string GroupName => nameof(ReleaseOnlyAttribute);

    /// <inheritdoc/>
    public override bool IsConditionMet => IsReleaseBuild;
}
