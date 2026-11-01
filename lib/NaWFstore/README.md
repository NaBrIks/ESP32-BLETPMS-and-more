# NaWFstore

Portable Wi-Fi profile management with ESP-IDF drivers. Independently authored source is MIT; see LICENSE. The library does not initialize or erase the application's NVS partition and never mounts a filesystem.

Core interfaces: IProfileStore, IWifiDriver, IClock. Manager is single-owner: adapters must serialize commands. ESP-IDF integration provides EspIdfWifiDriver, NvsProfileStore and EspClock; Console is a transport-independent incremental parser. HTTP and LVGL integration are application adapters.

NVS namespace defaults to `nawfstore`; versioned configurations use alternating `cfg0` and `cfg1` blobs. The SDK Wi-Fi storage remains RAM. Bootstrap profiles are imported only when no saved configuration exists; deliberately empty saved configurations remain empty. NVS is not encrypted automatically.

Build switches (CMake options and public C++ defines): `NAWFSTORE_WITH_NVS`, `NAWFSTORE_WITH_CONSOLE`, `NAWFSTORE_WITH_WEB`, `NAWFSTORE_WITH_SOFTAP`, `NAWFSTORE_WITH_BOOTSTRAP`; `NAWFSTORE_MAX_PROFILES` is 1..8. Disabled NVS gives the example application volatile configuration. Disabled console excludes its parser source. Application-specific HTTP adapter code should be guarded by WITH_WEB.

## Current application console

At 115200 baud, send `wifi help`. Enter accepts CR, LF and CRLF. Input is not echoed by firmware; disable local echo in your terminal when entering passwords.

- `wifi status`, `wifi list`
- `wifi add` or `wifi edit ID`: interactive SSID and password prompts; empty password means an open network, and editing replaces the password.
- `wifi remove ID`, `wifi priority ID NUMBER`, `wifi enable ID`, `wifi disable ID`
- `wifi connect ID`
- `wifi on [seconds]`, `wifi off`
- `wifi startup on|off [seconds]`: saved startup policy
- `wifi ap start [seconds]`, `wifi ap stop`: default setup window 300 seconds
- `wifi result TICKET`: asynchronous command completion

`priority` is signed; higher values are attempted first. Profile writes persist immediately; explicit connect applies a particular profile. Working connections are preserved during profile edits. A completed connect command means the attempt was started; check status for IP acquisition.

The application queues at most four commands and retains sixteen results. No password is returned by status/list or ordinary HTTP snapshots. AP uses an application-generated per-boot password, displayed only by explicit console AP-start or on the native setup screen. Its direct setup URL uses port 80. Captive DNS, scan UI and test-before-save are not implemented in this first version.

SDKs and toolchains belong in shared standard installation paths, never under the library or project.
