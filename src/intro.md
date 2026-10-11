# Introduction to Krypton

A memory-safe, high-performance Rust library and cryptographic utils for modern file encryption and secure vaults using Argon2id and AES-256-GSM with complete metadata protection along with a Universal Container Format for header.

Krypton is built for modern security standards and currently in alpha phase therefore the public API may change in future and may break.

`krypton` provides two complementary encrypted-
storage modes over one shared cryptographic core:

| Mode | Entry points | Use case |
|---|---|---|
| **Single-file** | `encrypt_file`, `decrypt_file` | Encrypt an individual file into a self-contained `.krf` container |
| **Vault** | `Vault` struct | A directory holding many entries (files *and* directories) under one password, with an encrypted index |

Both modes share:

- One universal wire format (`src/container.rs`) with a stable magic and
  structured versioning.
- One chunk engine (`src/stream.rs`, 64 KiB records).
- One primitive layer (`src/crypto.rs`): AES-256-GCM, Argon2id,
  HKDF-SHA256.

The crate is deliberately small: ~2.5k lines of library code across nine
modules, no async runtime, no I/O beyond `std::fs`.

```
src/
├── lib.rs          crate docs, prelude, re-exports
├── error.rs        Error enum (thiserror), Result alias
├── crypto.rs       AES-GCM / Argon2id / HKDF / CSPRNG wrappers
├── kdf.rs          KdfParams: storage, validation bounds
├── sanitize.rs     filename sanitization (traversal defense)
├── fsutil.rs       atomic writes, permission restriction, fsync helpers
├── stream.rs       chunk record writer/reader (format-agnostic)
├── container.rs    universal envelope: header, key derivation, trailer
├── single_file.rs  encrypt_file / decrypt_file
└── vault/
    ├── mod.rs      Vault state machine + public operations
    ├── keystore.rs vault.config: KEK-wrapped master key
    ├── manifest.rs encrypted BTreeMap index
    └── file_ops.rs per-entry blob write/read/delete
```

---


## Design goals and threat model

**Goals, in priority order:**

1. Confidentiality of content and metadata (filenames are encrypted).
2. Tamper *evidence*: any modification, truncation, reordering, splicing or
   transplant of ciphertext must be detected before plaintext is released.
3. No accidental data loss: corruption fails loudly; writes are atomic.
4. Simplicity auditable by one person: few moving parts, no legacy paths.

**Adversaries considered:** local attacker with read access to disk;
attacker with *write* access to stored files (tampering, swapping);
network/sync-folder replication of damaged or rolled-back files.

## Performance

Measured with the CLI release build (rustc 1.97.1, x86_64, AES-NI),
2026-08:

| Scenario | Time | Notes |
|---|---|---|
| Small file encrypt | **0.19 s** | dominated by Argon2id (64 MiB, t=4) — this floor *is* the brute-force defense; do not tune it down casually |
| 200 MB encrypt | 0.56 s (~360 MB/s) | single thread |
| 200 MB decrypt | 0.53 s (~380 MB/s) | roundtrip byte-identical |
| Vault add, 200 files | 0.68 s | includes 201 Argon2-free blob writes after one unlock |
| Vault list | 0.17 s | manifest-only, no blob I/O |
| Vault verify, 201 entries | 0.18 s | full AEAD pass over every blob |

### Analysis

- Bulk throughput sits around 5–10 % of raw AES-NI ceiling. Costs: single
  thread, 64 KiB chunks (allocation/GHASH overhead amortization), AAD
  construction per record. Chunks are already independent — a rayon-style
  parallel pipeline (roadmap 0.5.x) should scale near-linearly toward
  GB/s without format changes.
- Latency-sensitive UIs should batch: unlock once, add many. Per-vault-op
  cost after unlock is essentially AES speed.
- Verify is cheap relative to add because it skips KDF entirely (master
  already unlocked).

---

## Format versioning policy

Rules that keep the ecosystem stable forever:

1. The 8-byte magic **never changes**, including the NUL control byte.
2. Envelope revisions bump the `u16` version field; readers reject unknown
   versions with `UnsupportedVersion`. Released versions are readable
   forever.
3. A released `PayloadType` never changes meaning. New object kinds are
   new types with new AAD contexts.
4. Schema-bearing payloads (config v2, manifest v2) carry their own inner
   version integers, checked after authentication.
5. Pre-0.4 formats are retired, not emulated: readers were deleted in
   0.4. Migration = extract with 0.3 once, re-add with 0.4+.

---

## Public API tour

Everything public hangs off four re-exported items plus modules:

```rust
use krypton::{encrypt_file, decrypt_file, Vault};
use std::path::Path;

// Single file: encrypt, then decrypt back (name recovered from trailer).
let out = encrypt_file("hunter2", Path::new("report.pdf"), None)?;
let original_name = decrypt_file("hunter2", &out, Path::new("restored.pdf"))?;

// Vault workflow.
let mut vault = Vault::new(Path::new("myvault").to_path_buf());
vault.init("hunter2")?;
vault.unlock("hunter2")?;
vault.add(Path::new("docs"), Some("docs"))?;
for e in vault.list()? {
    println!("{} {} bytes", e.name, e.size);
}
vault.extract("docs/report.pdf", Path::new("out"))?;
vault.lock()?;
```

State machine: `init`/`unlock` require locked state; mutating operations
require unlocked; `verify` takes the password explicitly (does not latch
state). All fallible APIs return `Result` with the taxonomy above; no
panics on hostile input (test-enforced).

Module-level documentation in rustdoc mirrors this document; build with
`cargo doc --open`.

---

## Testing and quality gates

Current suite (all green at time of writing):

- **56 tests**: unit tests per module (crypto roundtrips/AAD binding,
  container tamper matrix, stream limits, sanitize cases, manifest
  truncation regression, vault lifecycle), integration tests
  (`tests/single_file.rs`, `tests/vault.rs`) covering multi-MB roundtrips,
  directory trees, password rotation and full-tamper sweeps.
- Gates enforced locally and in CI: `cargo test`, `clippy -D warnings`,
  `fmt --check`, `cargo doc` warning-free, `cargo publish --dry-run`.
- GitHub Actions: test matrix (stable + MSRV 1.75), lint job, docs job,
  publish-check; CLI repo builds release artifacts.

Notable regression tests born from audits:

- `truncated_manifest_is_an_error_not_an_empty_index` — protects against
  silent index wipe-out.
- `wrong_identity_rejected` — proves blob-transplant defense.
- Tamper-everywhere sweeps flip each header/body/trailer byte region and
  assert rejection.

---
