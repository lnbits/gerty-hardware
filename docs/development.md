# Development and releases

This guide covers building Gerty firmware, testing the USB web installer, and
publishing firmware and the installer through GitHub Actions. Run commands from
the repository root.

## Release files

| File | Purpose |
| --- | --- |
| `platformio.ini` | Device environments, toolchains and dependencies |
| `tools/package_firmware.py` | Release build flag, private-default check, merged images and installer manifests |
| `include/provisioning.h` | USB configuration protocol and persistent Wi-Fi/endpoint settings |
| `web/` | Installer page, configuration form and serial monitor |
| `.github/workflows/release.yml` | Tests, four firmware builds, GitHub Release uploads and Pages deployment |

Generated images and manifests live in `web/firmware/<environment>/` and are
ignored by Git. Commit the source files and workflow, not generated firmware.

## One-time GitHub setup

1. Push the repository, including `.github/workflows/release.yml`, to GitHub.
2. In repository **Settings → Pages → Build and deployment**, select
   **GitHub Actions** as the source. This workflow does not use a `gh-pages` branch.
3. Ensure repository or organization Actions policies allow the actions used by
   the workflow and its requested permissions: `contents: write`, `pages: write`
   and `id-token: write` for the publish job. No personal access token or firmware
   credentials need to be added as repository secrets.
4. If the `github-pages` environment has deployment restrictions, allow release
   tags. If it requires approval, a maintainer must approve the deployment.
5. Keep the workflow on the default branch so its manual **Run workflow** control
   is available.

Use the URL reported by the successful `github-pages` deployment. Without a
custom domain, this repository's expected project URL is
`https://blackcoffeexbt.github.io/gerty-v3/`. Forks use their own owner and repository
names. The deployment output is the authoritative URL.

## Local setup and checks

Use Python 3.11 and Node.js 22 to match the workflow. Install PlatformIO and
`intelhex` in a virtual environment:

```sh
python3 -m venv .venv
source .venv/bin/activate
python -m pip install platformio==6.1.19 intelhex
```

Run the installer tests and the existing Gerty protocol checks:

```sh
node --test tests/web_installer_test.cjs
python -m unittest discover -s tests -p package_firmware_test.py
c++ -std=c++11 -I include tests/gerty_protocol_test.cpp -o /tmp/gerty-protocol-test
/tmp/gerty-protocol-test
git diff --check
```

The workflow runs the JavaScript installer tests and Python packaging tests.
The C++ protocol check above is an additional local check. See the
[README hardware verification section](../README.md#hardware-verification) for
screen and network checks.

## Build release firmware locally

Release builds must set `GERTY_RELEASE=1`. This excludes `secrets.h` and compiled
endpoint defaults, so a new device waits for USB configuration. Packaging also
checks that private configuration defaults have not leaked into the application
image and fails if it finds them. Do not disable this check to publish a build.

Set `GERTY_VERSION` to the intended release tag. For example, replace `v1.0.0`
with the version you plan to release:

```sh
export GERTY_RELEASE=1
export GERTY_VERSION=v1.0.0
pio run -e T5-ePaper-S3 -e guition-JC3248W535 -e guition-JC4827W543 -t clean
pio run -e T5-ePaper-S3 -e guition-JC3248W535 -e guition-JC4827W543
pio run -e waveshare-ESP32-C6-LCD-1_3 -t clean
pio run -e waveshare-ESP32-C6-LCD-1_3
unset GERTY_RELEASE GERTY_VERSION
```

Run the S3 and C6 builds sequentially. Their toolchains use overlapping package
names; concurrent installation in a shared PlatformIO home can break a build.
GitHub Actions gives each device its own runner.

Cleaning before the release build ensures the packaging hook runs even when only
the version or installer copy changed. For each environment, check for:

- `web/firmware/<environment>/firmware.bin`: a merged image flashed at offset `0`.
- `web/firmware/<environment>/manifest.json`: the correct version, chip family and
  relative image path.

The supported environments are:

| Environment | Chip | Image dimensions |
| --- | --- | --- |
| `T5-ePaper-S3` | ESP32-S3 | 960 × 540 |
| `guition-JC3248W535` | ESP32-S3 | 480 × 320 |
| `guition-JC4827W543` | ESP32-S3 | 480 × 272 |
| `waveshare-ESP32-C6-LCD-1_3` | ESP32-C6 | 240 × 240 |

## Preview and test the web installer

```sh
python3 -m http.server 8000 --directory web
```

Open `http://localhost:8000` in desktop Chrome or Edge. Localhost permits Web
Serial; public hosting requires HTTPS. Internet access is needed for the pinned
ESP Web Tools module. Without generated firmware, the page shows that no build
is available and hides the installation button.

Before release, verify on each device:

1. Select its exact model and install firmware using a USB data cable. The chip
   check cannot distinguish the three S3 displays from each other.
2. Close the installation dialog, then select **Connect to configure**. Only one
   serial connection can own the USB port at a time.
3. Set up a device in the LNbits Gerty extension, matching the image dimensions
   above. Enter its base pages endpoint and a reachable 2.4 GHz Wi-Fi network.
4. Save and confirm the page reports a device acknowledgement. Allow about a
   minute for startup, then check serial logs for Wi-Fi and image download results.
5. Confirm the display shows the expected image. Power-cycle it and check that
   saved settings still work.
6. Press RST without BOOT and reconnect within 60 seconds to change settings.
   A new unconfigured device waits indefinitely. LilyGO disconnects USB during
   deep sleep, so reconnect when it wakes or reset it.
7. Check log viewing, clearing and downloading. Review logs before sharing because
   they can contain the private endpoint.

Browser installation uses a full merged image and erases saved settings. Users
must configure again after installing. Ordinary PlatformIO app-only uploads can
preserve settings when flash is not erased.

## Publish a release

### 1. Prepare the commit

Review and commit all intended source changes, including new untracked files.
Confirm tests and hardware checks above, then push the release commit to the
appropriate branch. A tag contains only committed files.

```sh
git status --short
git diff --check
git log -1 --oneline
git push origin HEAD
```

### 2. Create and push an annotated tag

Use a new version; `v1.0.0` below is an example. Check that it is unused before
creating it. Do not move or force-push an existing release tag.

```sh
git fetch origin --tags
git tag --list v1.0.0
git tag -a v1.0.0 -m "Gerty v1.0.0"
git show --no-patch v1.0.0
git push origin v1.0.0
```

Continue with tag creation only if the tag-list command returned no matching tag.
The workflow triggers on **every pushed tag**, not just `v*` tags. A normal branch
push does not trigger it.

### 3. Follow the workflow

Open **Actions → Firmware and web installer** and select the tag run. It:

1. Runs installer and packaging tests.
2. Builds all four devices with `GERTY_RELEASE=1` and the tag as `GERTY_VERSION`.
3. Uploads each device's image and manifest as a workflow artifact.
4. After all tests and builds pass, assembles the `web/` site and release downloads.
5. Creates a GitHub Release with generated notes if one does not already exist,
   then uploads four `<environment>.bin` files, `README.md` and `SHA256SUMS`.
6. Deploys the installer and firmware to GitHub Pages.

These releases are published automatically; the workflow does not create drafts
or automatically mark prereleases based on the tag name. Review the generated
release notes and add user-facing changes and any hardware limitations.

### 4. Verify publication

- Confirm all jobs passed, including the Pages deployment.
- Open the GitHub Release and check all four `.bin` downloads and `SHA256SUMS`.
- To verify downloaded binaries, put them beside `SHA256SUMS` and run
  `sha256sum -c SHA256SUMS` (Linux) or `shasum -a 256 -c SHA256SUMS` (macOS).
- Open the Pages URL from the deployment. Refresh the page and select each device;
  the displayed firmware version should match the tag.
- Check that each installation button is available and its manifest and firmware
  requests succeed. Repeat a USB installation from the published site.

## Installer-only updates and manual deployment

Installer HTML, styling and scripts are checked out from the same ref as firmware.
Pushing web changes to a branch alone does not update Pages.

For a versioned public update, commit the changes and push a new release tag using
the steps above. To deploy without creating a new tag, use **Actions → Firmware
and web installer → Run workflow**, selecting the intended branch. A manual
branch run rebuilds all four devices, uses the branch name as the displayed
version, and deploys Pages; it skips GitHub Release creation and asset uploads.

Every successful deployment replaces the version offered on Pages. There is no
version selector or semantic-version ordering. A manual branch run or rerun of an
older tag can replace a newer site's firmware. Release downloads from other tags
remain available on GitHub.

## Failed runs and recovery

- **Tests or build fails:** publication does not start. Fix the source, validate
  locally, and release a new tag. Rerun the existing run for transient download or
  runner failures when no source change is needed.
- **Private-default check fails:** check that the release flag is applied before
  compilation and clean/rebuild. Keep credentials and private endpoints out of
  release firmware.
- **Pages fails or waits for approval:** check the Pages source, workflow
  permissions and `github-pages` environment rules, including tag eligibility.
  Release assets may already exist because release upload happens before Pages.
- **Need to retry publication:** rerunning the tag workflow reuses the release
  and overwrites same-named assets (`--clobber`). This is not an immutable artifact
  store; retain checksums when comparing previous downloads.
- **Need to restore the previous installer:** rerun a known-good tag's workflow.
  This rebuilds and redeploys that tag and overwrites its same-named release assets.
  It does not modify devices already flashed. Prefer a new corrective release when
  firmware changes are required.

Keep dependency and licensing notices when distributing firmware. See
[README → Dependencies](../README.md#dependencies), including the LilyGO driver's
GPL-3.0 licensing information.
