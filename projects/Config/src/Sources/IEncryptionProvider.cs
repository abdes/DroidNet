// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Security.Cryptography;

namespace DroidNet.Config.Sources;

/// <summary>
///     Defines a cryptographic service that encrypts and decrypts binary data.
/// </summary>
/// <remarks>
///     This interface is binary-first and algorithm-agnostic. Implementations should:
///     - Use well-established, vetted algorithms (e.g., AES-GCM for symmetric encryption).
///     - Separate cryptographic operations from key management. Keys and IVs/nonces must be supplied
///       via secure channels (e.g., environment variables, DPAPI, Azure Key Vault).
///     - Validate inputs and fail fast on misuse (e.g., null, empty data when unsupported).
///     - Prefer authenticated encryption (AEAD) to ensure confidentiality and integrity; throw on tampering.
///     - Be explicit about determinism. Deterministic encryption (same plaintext → same ciphertext) is generally
///       discouraged unless a clear threat model allows it. Nonces/IVs should be unique per operation when required.
///     - Document thread-safety: stateless implementations are typically thread-safe; stateful ones are not.
///     <para>
///     This interface targets small-to-medium buffers. For large data, we may consider as a future enhancement to
///     provide alternative APIs (e.g., streams) or chunking strategies.
///     </para>
/// </remarks>
public interface IEncryptionProvider
{
    /// <summary>
    ///     Encrypts the supplied plaintext bytes and returns the ciphertext bytes.
    /// </summary>
    /// <param name="data">The plaintext to encrypt. Must not be <see langword="null" />.</param>
    /// <returns>
    ///     The ciphertext bytes produced by the algorithm. Format may include nonces/IVs, authentication tags,
    ///     and metadata as defined by the implementation.
    /// </returns>
    /// <exception cref="ArgumentNullException">
    ///     Thrown when <paramref name="data"/> is <see langword="null" />.
    /// </exception>
    /// <exception cref="CryptographicException">
    ///     Thrown when the encryption operation fails (e.g., invalid key/nonce, internal crypto error).
    /// </exception>
    /// <remarks>
    ///     Implementations should:
    ///     - Generate and manage per-operation nonces/IVs when required, ensuring uniqueness.
    ///     - Include integrity protection (e.g., authentication tag) in the returned payload if using AEAD.
    ///     - Define the payload layout (e.g., [nonce | ciphertext | tag]) and keep it stable for interoperability.
    /// </remarks>
    public byte[] Encrypt(byte[] data);

    /// <summary>
    ///     Decrypts the supplied ciphertext bytes and returns the plaintext bytes.
    /// </summary>
    /// <param name="data">
    ///     The ciphertext to decrypt. Must not be <see langword="null" />. The format must match the implementation’s expected layout,
    ///     including any embedded nonces/IVs and authentication tags.
    /// </param>
    /// <returns>The decrypted plaintext bytes.</returns>
    /// <exception cref="ArgumentNullException">
    ///     Thrown when <paramref name="data"/> is <see langword="null" />.
    /// </exception>
    /// <exception cref="CryptographicException">
    ///     Thrown when decryption fails (e.g., corrupted input, authentication tag mismatch, wrong key/nonce).
    ///     Consumers should treat this as a security signal and not ignore it.
    /// </exception>
    /// <remarks>
    ///     Implementations should:
    ///     - Verify integrity/authenticity prior to returning plaintext (AEAD strongly recommended).
    ///     - Produce clear, minimal errors without leaking sensitive details (avoid detailed crypto state in messages).
    ///     - Avoid partial plaintext output on failure; fail atomically.
    /// </remarks>
    public byte[] Decrypt(byte[] data);
}
