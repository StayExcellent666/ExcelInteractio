# Future updater requirements

The local experimental updater is adapted from ExcelProtocol FX and uses the hard-coded repository `StayExcellent666/ExcelInteractio`. Update policy, helper checksum checks and an isolated installer smoke test have been run. A full update after OBS exits remains unverified, and no release has been published. Complete that test before publishing.

- Updates section: automatic checking and automatic download/install options default enabled for new users while preserving saved preferences. Stable is the default channel, with an optional beta channel and manual check.
- Check roughly 20 seconds after OBS starts and every 24 hours. Reject drafts, malformed versions, downgrades and incompatible releases.
- Auto-install prepares and verifies the update, then offers Restart OBS Now or Later. Disable immediate restart during streaming or recording. Later queues the update until normal exit, without closing or restarting OBS. Allow cancellation.
- A separate native helper waits for the launching OBS process to exit, refuses installation if any other OBS process is running, never forces OBS closed, and allows Windows elevation when necessary. Only the explicit Restart Now choice requests normal OBS shutdown and relaunches after a successful install.
- Preserve or roll back the existing installation on failure and report the result on the next launch.
- Publish a Windows installer and update manifest for every release. Verify repository, release, version, channel, Windows platform, OBS compatibility, asset names, sizes and SHA-256 digests before staging. Use HTTPS with certificate validation, restrict redirects to trusted GitHub asset hosts, and bound download size and time.
- The helper verifies the installer SHA-256 again immediately before execution. Never execute commands from release metadata.
- Test the policy and an end-to-end update in an isolated OBS installation before publishing.
