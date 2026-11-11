# Host provisioning: preserve existing SD packs

Source changes: `tools/pack-builder/provision_pack.py`, its README and
`tests/test_provision_pack.py`. No device, SD card, publication or dependency
pin changes were made.

The existing staging function copied directly over matching filenames and
accepted a missing manifest or same-size corrupt archive. New regressions
reproduced these failures before implementation (red run: 22 tests, six
failures and one error). Staging now requires declared size/SHA-256, copies
into a hidden directory, verifies the copied bytes, generates the checksum,
flushes files and publishes the manifest last. A name reservation serializes
cooperating provisioners. Existing target files are refused; exception cleanup
removes only files published by this attempt. Extraction directly into the
live pack directory is refused, including with `--force`.

Run from the OrcMaps worktree root:

```powershell
python -m unittest discover -s tests -p test_provision_pack.py
```

Actual final result: **24 tests passed**. Cases cover old-file preservation,
missing manifest, same-size hash mismatch, corruption during copying, copy and
activation failures, manifest-last order and direct-build rejection. The first
green attempt exposed Windows `fsync` rejecting a read-only file handle; the
implementation now opens that handle read/write and the rerun passed.

Saved logs: `provision-stage-red-20261019.log` and
`provision-stage-green-20261019.log` in the invocation directory.

This does not authenticate local manifests, perform complete engine schema
validation, or implement device download/resume. Use the engine pack verifier
as a separate compatibility gate. The card must be unmounted from the device
and exclusively owned by the host. Sudden power loss, directory durability,
orphan recovery and actual removable-media behavior remain unverified; hidden
reservations or unreferenced archives can remain after process/power loss.
