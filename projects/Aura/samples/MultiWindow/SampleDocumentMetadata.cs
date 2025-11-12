// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Aura.Documents;

namespace DroidNet.Samples.Aura.MultiWindow;

public sealed class SampleDocumentMetadata : IDocumentMetadata
{
    public Guid DocumentId { get; set; } = Guid.NewGuid();

    public string Title { get; set; } = string.Empty;

    public Uri? IconUri { get; set; }

    public bool IsDirty { get; set; }

    public bool IsPinnedHint { get; set; }
}
