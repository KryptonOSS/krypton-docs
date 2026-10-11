# Roadmap

Security-first priorities, derived from the [Known Limitations](security.html) and from audit findings. Items are grouped by horizon; within
a group they are roughly priority-ordered. Anything that weakens a security
property will never be accepted for a compatibility or convenience win.

## Now (next release)

- **Fuzzing targets** — `cargo-fuzz` harnesses for `read_container`,
  `read_entry`, manifest/config parsing and name sanitization. The parser
  surface is small; it should be provably robust against hostile input.
- **Memory locking** — evaluate `memsec`/`region` behind a tiny `unsafe`
  boundary (feature-gated) so master keys can be `mlock`ed and core dumps
  suppressed. Requires relaxing `forbid(unsafe_code)` to
  `deny(unsafe_code)` with audited allow-lists.
- **Rollback protection (limitation 6)** — store a MAC'd monotonically
  increasing generation counter in the vault config and sign each manifest
  save with it, so restoring an old snapshot of the vault directory is at
  least *detectable* on next unlock.
- **CI hardening** — Windows job (rename semantics differ), MSRV job,
  `cargo-audit` / `cargo-deny` supply-chain checks, and release artifacts.

## Next (0.5.x)

- **Recovery kit** — printable recovery code that wraps the master key
  under a second Argon2id-derived key, stored alongside `vault.config`.
  Mitigates the "one config file bricks everything" failure mode without
  weakening the single-password model.
- **Multiple keyslots (LUKS-style)** — several independent passphrases per
  vault, each its own wrapped copy of the master key; individual slots can
  be revoked. Depends on the config schema gaining a slot table.
- **Content re-encryption on password change (limitation 2)** — optional
  deep rotation: derive a fresh master key and stream every blob through
  re-encryption. Slow by design; opt-in only.
- **Parallel chunk pipeline** — encrypt/decrypt chunks across threads
  (counter nonces and AAD already make chunks independent). Target:
  saturate AES-NI (~3–5 GB/s) instead of today's ~400 MB/s single-thread.
- **Streaming public API** — `Read`/`Write` adapter types so library users
  can pipe containers without buffering whole objects.
- **Orphan garbage collection** — after failed adds/removals, blobs may
  exist that no manifest record references. Add a `verify --prune` /
  compaction routine that walks `d/` and deletes unreferenced blobs.
- **Dependency slimming: drop `walkdir`** — its single production use is
  the directory traversal in `add_directory_tree` (`src/vault/mod.rs`).
  Replace with a ~40-line `std::fs::read_dir` walker preserving exact
  semantics (pre-order, siblings sorted by file name, parents before
  children, symlinks and special files skipped). Removes `walkdir` and
  `same-file` from every consumer's build. Pure hygiene win; existing
  nested-directory tests guard against behavior drift. Considered and
  rejected for now to avoid touching a verified codebase.

## Later (1.0 candidates)

- **Hardware-backed keys** — optional second factor via OS key stores
  (TPM, Secure Enclave, FIDO2/hmac-secret) combined with the password
  (limitation 4). Must remain strictly optional; plain password vaults are
  the default and always supported.
- **Size hiding (limitation 5)** — optional fixed-size padding buckets
  for `.krf` output and/or random trailing padding before the end marker,
  trading space for length privacy. Needs care: padding must be covered by
  the trailer's authenticated `size`.
- **External security audit** — pay for a professional review of the
  container format, key hierarchy and implementation before calling 1.0
  production-grade. No amount of internal review substitutes for this.
- **Constant-time decisions** — review any branch on secret material
  (currently none known: all comparisons are inside AEAD verification),
  and document the argument formally in SECURITY.md.

## Explicitly out of scope

- **Plausibility/deniability (limitation 3)** — the magic is a feature;
  hidden-volume schemes conflict with authenticated formats and invite
  user-error catastrophes. Revisit only with a rigorous design.
- **Backward compatibility shims** — pre-0.4 formats stay retired.
  Migration is a one-time `krypton migrate` helper only if demand exists.

