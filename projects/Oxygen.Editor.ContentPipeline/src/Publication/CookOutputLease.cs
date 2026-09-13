// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ContentPipeline.Publication;

/// <summary>Coordinates project-output readers and publication across processes.</summary>
public static partial class CookOutputLease
{
    /// <summary>Registers a reader before it opens published files.</summary>
    /// <param name="projectRoot">The owning project's absolute directory.</param>
    /// <returns>A lease retained until all reader work and file handles have drained.</returns>
    public static IDisposable AcquireRead(string projectRoot)
    {
        var (_, gatePath, readers) = PreparePaths(projectRoot);
        using var gate = OpenGate(gatePath);
        return CreateReader(readers);
    }

    /// <summary>Excludes new readers and requires existing readers to have released ownership.</summary>
    /// <param name="projectRoot">The owning project's absolute directory.</param>
    /// <returns>The publication lease, including an atomic handoff to runtime readers.</returns>
    public static CookOutputWriteLease AcquireWrite(string projectRoot)
    {
        var (root, gatePath, readers) = PreparePaths(projectRoot);
        var gate = OpenGate(gatePath);
        try
        {
            RemoveReleasedReaders(readers);
            RemoveReleasedReaders(readers, "*.scan");
            var lease = new CookOutputWriteLease(root, readers, gate);
            gate = null;
            return lease;
        }
        finally
        {
            gate?.Dispose();
        }
    }

    /// <summary>Creates a reader while the caller owns the registration gate.</summary>
    /// <param name="directory">The project's reader directory.</param>
    /// <param name="extension">Distinguishes persistent preview readers from finite status scans.</param>
    /// <returns>The live marker owner.</returns>
    internal static IDisposable CreateReader(string directory, string extension = ".lease")
    {
        var path = Path.Combine(directory, Guid.NewGuid().ToString("N") + extension);
        var stream = new FileStream(path, FileMode.CreateNew, FileAccess.ReadWrite, FileShare.None);
        try
        {
            var reader = new ReaderLease(path, stream);
            stream = null;
            return reader;
        }
        finally
        {
            stream?.Dispose();
        }
    }

    /// <summary>Rejects filesystem redirection in operation-owned paths.</summary>
    /// <param name="path">The path about to be used.</param>
    internal static void RejectReparsePoint(string path)
    {
        var attributes = new FileInfo(path).Attributes;
        if (attributes != (FileAttributes)(-1) && attributes.HasFlag(FileAttributes.ReparsePoint))
        {
            throw new IOException($"Cook ownership paths must not redirect outside the project: '{path}'.");
        }
    }

    /// <summary>Retains one private operation through native work and publication.</summary>
    /// <param name="projectRoot">The project that owns the operation.</param>
    /// <param name="operationId">The private directory identity.</param>
    /// <returns>The process-owned operation lease.</returns>
    internal static FileStream AcquireOperation(string projectRoot, Guid operationId)
    {
        var (root, _, _) = PreparePaths(projectRoot);
        var directory = Path.Combine(root, ".build", "cook", operationId.ToString("N"));
        RejectReparsePoint(directory);
        _ = Directory.CreateDirectory(directory);
        var path = Path.Combine(directory, "operation.lock");
        RejectReparsePoint(path);
        try
        {
            return new FileStream(path, FileMode.OpenOrCreate, FileAccess.ReadWrite, FileShare.None, 1, FileOptions.DeleteOnClose);
        }
        catch (IOException exception) when (IsSharingConflict(exception))
        {
            throw new CookOutputBusyException("The cook operation still owns native or publication work.", exception);
        }
    }

    private static (string projectRoot, string gate, string readers) PreparePaths(string projectRoot)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(projectRoot);
        var root = Path.GetFullPath(projectRoot);
        var attributes = new DirectoryInfo(root).Attributes;
        if (attributes == (FileAttributes)(-1) || !attributes.HasFlag(FileAttributes.Directory))
        {
            throw new DirectoryNotFoundException($"The project directory does not exist: '{root}'.");
        }

        var build = Path.Combine(root, ".build");
        var cook = Path.Combine(build, "cook");
        var readers = Path.Combine(cook, "readers");
        foreach (var directory in new[] { build, cook, readers })
        {
            RejectReparsePoint(directory);
            _ = Directory.CreateDirectory(directory);
        }

        var gate = Path.Combine(cook, "publication.lock");
        RejectReparsePoint(gate);
        return (root, gate, readers);
    }

    private static FileStream OpenGate(string path)
    {
        try
        {
            return new FileStream(path, FileMode.OpenOrCreate, FileAccess.ReadWrite, FileShare.None);
        }
        catch (IOException exception) when (IsSharingConflict(exception))
        {
            throw new CookOutputBusyException("Another publication is using this project's cooked content. Retry when it finishes.", exception);
        }
    }

    private static void RemoveReleasedReaders(string directory, string pattern = "*.lease")
    {
        foreach (var path in Directory.EnumerateFiles(directory, pattern))
        {
            if (!Guid.TryParseExact(Path.GetFileNameWithoutExtension(path), "N", out _))
            {
                throw new InvalidDataException($"An unrecognized output-reader lease must be reviewed: '{path}'.");
            }

            RejectReparsePoint(path);
            try
            {
                using (var reader = new FileStream(path, FileMode.Open, FileAccess.ReadWrite, FileShare.None))
                {
                    if (reader.Length != 0)
                    {
                        throw new InvalidDataException($"An invalid output-reader lease must be reviewed: '{path}'.");
                    }
                }

                File.Delete(path);
            }
            catch (FileNotFoundException)
            {
                // The reader released and removed its marker after enumeration.
                continue;
            }
            catch (IOException exception) when (IsSharingConflict(exception))
            {
                throw new CookOutputBusyException("Another preview is reading cooked content. Close it and retry.", exception);
            }
        }
    }

    private static bool IsSharingConflict(IOException exception) => (exception.HResult & 0xffff) is 32 or 33;

    private sealed partial class ReaderLease(string path, FileStream stream) : IDisposable
    {
        private readonly Lock sync = new();
        private FileStream? stream = stream;

        public void Dispose()
        {
            lock (this.sync)
            {
                if (this.stream is null)
                {
                    return;
                }

                this.stream.Dispose();
                this.stream = null;
            }

            try
            {
                File.Delete(path);
            }
            catch (IOException exception) when (IsSharingConflict(exception))
            {
                // A writer has claimed the released marker and will remove it.
            }
        }
    }
}
