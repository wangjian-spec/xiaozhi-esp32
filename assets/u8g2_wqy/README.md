Place Wenquanyi BDF-binary font files here so they get packed into the default `assets` partition.

Expected filenames (must match exactly):
- `wenquanyi_9pt.bin`
- `wenquanyi_10pt.bin`
- `wenquanyi_11pt.bin`
- `wenquanyi_12pt.bin`
- `wenquanyi_13pt.bin`

Notes:
- The assets packer flattens directories, so only the filename matters.
- After adding the files, rebuild and flash. You should no longer see logs like:
  `BDF font not found in assets: wenquanyi_11pt.bin`
