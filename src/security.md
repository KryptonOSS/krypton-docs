# Threat Model

Krypton protects **data at rest** against an adversary with read access to the
storage medium: stolen laptops, leaked cloud backups, seized disks. The
adversary may also attempt to modify stored ciphertext.

Out of scope: malware on the user's machine while a vault is unlocked,
keyloggers, compromised OS, side channels on the CPU (e.g. Rowhammer),
and coercion of the password itself.

## Cryptographic Design

    | Primitive | Role |
    |---|---|
    | AES-256-GCM | AEAD for all ciphertext (chunks, trailers, manifests, wrapped keys) |
    | Argon2id (64 MiB, t=4, p=4) | Password → KEK; parameters serialized per container |
    | HKDF-SHA256 | Master key → per-object subkeys; manifest key derivation |
    | SHA-256 | Blob path derivation (`SHA256(master ‖ path)`), verification hash |

### Nonces

- **Single-file / vault entries:** 12-byte nonce = `0x0000_0000 ‖ chunk_index (u64 BE)`
  under an HKDF subkey unique to the object and its salt. Counter nonces are
  collision-free within an object regardless of size or restarts.
- **Trailer:** reserved all-`0xFF` nonce, distinct domain from data chunks.
- **Manifest & config wrap:** fixed nonces under keys that are themselves
  random per vault/manifest generation — safe because the *input* key material
  is never reused across encryptions.

### Associated Data

Every AEAD operation binds the container's payload-type context
(`"krypton-container-v1/file"`, …) plus the chunk index, and the trailer
additionally binds a `/trailer` suffix. Consequences:

- Reordering chunks between positions → authentication failure.
- Truncating the file → detected by trailer cross-check of `size`/`chunk_count`.
- Moving bytes between two `.krf` files (same password!) → fails because each
  container derives an independent object salt/subkey.
- The unauthenticated end-of-chunks marker cannot be exploited: the trailer is
  authenticated and its `chunk_count` must match exactly what preceded it.

### Key Hierarchy

```
password ─Argon2id─► KEK ─wrap─► master key (vault.config)
                                    ├─► HKDF subkeys per entry blob
                                    └─► HKDF manifest key
```

`change_password` re-wraps only the master key; no re-encryption of data.
Password rotation does not invalidate old ciphertext copies an attacker may
have previously exfiltrated.

## Memory Handling

- All keys are held in `Zeroizing<[u8; 32]>`; buffers are zeroized on drop.
- Keys are never copied by value into logs, errors, or serialization.
- The crate is compiled with `#![forbid(unsafe_code)]`; we do not use mlock
  or core-dump suppression (see Limitations).

## Filesystem Hardening

- Atomic writes: temp file in the same directory + `fsync` + `rename`, so a
  crash mid-write can never corrupt a vault config or manifest.
- Vault directories and `.krf` outputs are created with mode `0700`/`0600`
  on Unix.
- Names read back from containers are sanitized before path joins;
  absolute paths, `..`, separators and control characters are rejected —
  a malicious vault archive cannot write outside its directory.
- Directory removal walks the encrypted manifest's child lists, so removing
  a tree leaves no orphaned blobs.

## Format Versioning Policy

Since 0.4, all objects use one universal container: an 8-byte magic
(`KRYPTON\0`, with a control byte so it can never occur in plain text),
a u16 envelope version, a key-derivation kind byte (HKDF-from-master for
vault objects, Argon2id password for standalone files), chunk records and
an authenticated trailer carrying the payload type (`file`, `directory`,
`manifest`).

Rules that keep this stable:

1. The magic string never changes. Envelope revisions bump the version
   field; readers reject unknown versions with `UnsupportedVersion`.
2. A released payload type never changes meaning; new object kinds are
   new trailer types with new context strings.
3. Pre-0.4 formats are retired: readers were removed in 0.4 and old files
   report `Error::RetiredFormat`. Extract data with 0.3 before upgrading.

## Verification API

`Vault::verify()` authenticates every blob and the manifest, reporting a
per-entry integrity report without writing plaintext to disk.

## Known Limitations

1. **No memory locking** — with `forbid(unsafe_code)` we cannot call
   `mlock`; unlocked keys may hit swap if the system has no encrypted swap.
2. **No key rotation of content** — changing the password re-wraps the
   master key but old ciphertext snapshots remain decryptable with the old
   password forever.
3. **No deniability** — format magics and structure identify files as
   Krypton containers.
4. **Single-factor** — no hardware-token or multi-party key splitting.
5. **Filename leakage via size** — plaintext lengths are visible through
   ciphertext length (chunked at 64 KiB granularity).
6. **Trust on first unlock** — the config stores a verification hash rather
   than a signature; there is no protection against full-storage rollback to
   an older valid vault snapshot.

## Reporting

Please report vulnerabilities privately to the maintainers instead opening a
public issue.

