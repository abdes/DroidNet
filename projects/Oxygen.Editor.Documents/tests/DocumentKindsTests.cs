// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Documents.Tests;

/// <summary>
/// A document's title: its file name without the suffix its kind opens, unless the kind's title keeps the suffix, as a
/// model's does ("Car.glb", model editor 4).
/// </summary>
[TestClass]
public sealed class DocumentKindsTests
{
    private static readonly FileSystemPath Wood = FileSystemPath.From(Path.Combine(Path.GetTempPath(), "Wood" + NoteKind.Suffix));

    [TestMethod]
    public void TitleOf_KindDroppingItsSuffix_IsTheNameWithoutIt()
        => _ = DocumentKinds.TitleOf(new NoteKind(), Wood).Should().Be("Wood");

    [TestMethod]
    public void TitleOf_KindKeepingItsSuffix_IsTheFileName()
        => _ = DocumentKinds.TitleOf(new NoteKind { TitleKeepsSuffix = true }, Wood).Should().Be("Wood" + NoteKind.Suffix);
}
