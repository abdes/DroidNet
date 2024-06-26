// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Docking;

using System.Diagnostics.CodeAnalysis;
using System.Runtime.CompilerServices;
using DroidNet.Docking.Detail;
using DroidNet.Docking.Workspace;
using VerifyTests.DiffPlex;

/// <summary>Module level initialization code for the test frameworks.</summary>
[ExcludeFromCodeCoverage]
public static class ModuleInitializer
{
    /// <summary>Initialize the <see href="https://github.com/VerifyTests">`Verify`</see> test framework.</summary>
    [ModuleInitializer]
    public static void Init()
    {
        VerifyDiffPlex.Initialize(OutputType.Compact);

        // Ignore auto-incremented IDs that will make test output unpredictable
        VerifierSettings.IgnoreMember(typeof(LayoutSegment), nameof(LayoutSegment.DebugId));
        VerifierSettings.IgnoreMember(typeof(ILayoutSegment), nameof(ILayoutSegment.Docker));
        VerifierSettings.IgnoreMember(typeof(IDock), nameof(IDock.Id));
        VerifierSettings.IgnoreMember(typeof(IDock), nameof(IDock.Docker));
        VerifierSettings.IgnoreMember(typeof(Dock), nameof(Dock.GroupInfo));
        VerifierSettings.IgnoreMember(typeof(LayoutEngine.Flow), nameof(LayoutEngine.Flow.Description));
    }
}
