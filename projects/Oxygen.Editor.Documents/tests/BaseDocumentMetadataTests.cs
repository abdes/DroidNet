// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;

namespace Oxygen.Editor.Documents.Tests;

/// <summary>Checks the saved state a document reports to its tab and close prompt.</summary>
[TestClass]
public sealed class BaseDocumentMetadataTests
{
    /// <summary>A document whose file is missing on disk is unsaved until the file is back, without counting as an edit.</summary>
    [TestMethod]
    public void MissingOnDiskMakesADocumentUnsavedUntilItsFileIsBack()
    {
        var metadata = new TestDocumentMetadata();
        _ = metadata.IsDirty.Should().BeFalse();

        metadata.IsMissingOnDisk = true;
        _ = metadata.IsDirty.Should().BeTrue();
        _ = metadata.ChangeVersion.Should().Be(metadata.SavedVersion, "a missing file is not an edit");

        metadata.IsMissingOnDisk = false;
        _ = metadata.IsDirty.Should().BeFalse();
    }

    /// <summary>Saving edits leaves a missing document unsaved: only writing its file again clears the mark.</summary>
    [TestMethod]
    public void SavedEditsDoNotHideAMissingFile()
    {
        var metadata = new TestDocumentMetadata { IsMissingOnDisk = true };
        metadata.IsDirty = true;

        metadata.MarkSaved(metadata.ChangeVersion);

        _ = metadata.IsDirty.Should().BeTrue();
    }
}
