# Android Auto development credentials

Switch Auto does not distribute Android Auto head-unit credentials.

For local development, place the credential pair here before building:

- `headunit.crt`
- `headunit.key`

The build packages these local files in RomFS, but Git ignores them. At
runtime the Switch implementation prefers files supplied on the SD card:

- `sdmc:/switch/switch-auto/android-auto/headunit.crt`
- `sdmc:/switch/switch-auto/android-auto/headunit.key`

RomFS credentials are a development convenience only. Production credentials
must be provisioned securely and must not be committed to the repository.
