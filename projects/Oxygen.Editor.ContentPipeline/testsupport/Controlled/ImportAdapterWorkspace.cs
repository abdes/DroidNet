// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ContentPipeline.TestSupport;

internal sealed partial class ImportAdapterWorkspace : IDisposable
{
    public ImportAdapterWorkspace()
    {
        this.Root = Path.Combine(Path.GetTempPath(), "oxygen-import-tool-api-tests", Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(this.Root);
        this.ToolPath = Path.Combine(this.Root, "Oxygen.Cooker.ImportTool.exe");
        File.WriteAllText(this.ToolPath, "Test tool");
        File.WriteAllText(Path.Combine(this.Root, "Oxygen.Cooker.Inspector.exe"), "Test Inspector");
        this.Compatibility = new(
        [
            new(Oxygen.Managed.Core.Compatibility.NativeArtifactInventory.ImportToolId, this.ToolPath),
                new(Oxygen.Managed.Core.Compatibility.NativeArtifactInventory.SourceAnalysisSchemaId, Path.Combine(AppContext.BaseDirectory, "Schemas", "oxygen.source-analysis.schema.json")),
                new(Oxygen.Managed.Core.Compatibility.NativeArtifactInventory.CapturedInputsSchemaId, Path.Combine(AppContext.BaseDirectory, "Schemas", "oxygen.captured-inputs.schema.json")),
            ]);
    }

    public string Root { get; }

    public string ToolPath { get; }

    public Oxygen.Testing.TemporaryNativeArtifacts Compatibility { get; }

    public void Dispose()
    {
        this.Compatibility.Dispose();
        if (Directory.Exists(this.Root))
        {
            Directory.Delete(this.Root, recursive: true);
        }
    }
}
