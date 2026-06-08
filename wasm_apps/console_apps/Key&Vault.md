# AkiraKey & AkiraVault — User Guide

---

## Button layout

| Button | GPIO | Action |
|--------|------|--------|
| UP     | 4    | Scroll up / increment digit |
| DOWN   | 5    | Scroll down / decrement digit |
| LEFT   | 6    | Back / cancel |
| RIGHT  | 7    | Confirm / enter / advance |
| A      | 15   | Confirm (AkiraVault) / type via BLE (AkiraKey) |
| B      | 16   | AkiraKey: long-press 2.5 s = immediate lock |
| HOME   | 0    | Back / shortcut to lock |

---

## AkiraVault — Cold Crypto Wallet

AkiraVault stores a BIP-39 seed phrase and derived accounts entirely on-device.
The vault is encrypted with a 6-digit PIN using PBKDF2 (10 000 rounds).
Nothing is ever transmitted — it is an air-gapped cold wallet.

### First launch — setup

1. **Welcome screen** — two options:
   - **Generate** (default) — creates a fresh 24-word BIP-39 seed phrase
   - **Restore** — enter an existing seed phrase word by word
   - Use **UP/DOWN** to switch, **RIGHT** to confirm.

2. **Generate screen** — seed phrase shown 6 words per page (4 pages).
   - Press **RIGHT** to advance, **LEFT** to go back.
   - Write every word down offline in order — this is your only backup.

3. **Unlock screen** — set your 6-digit PIN:
   - **UP/DOWN** change the current digit (0–9).
   - **RIGHT** advances to the next digit and confirms on the 6th.
   - **LEFT** steps back one digit.

### Unlock (subsequent launches)

Enter your 6-digit PIN the same way: **UP/DOWN** change digit, **RIGHT** advance/confirm.

> **Warning:** entering the wrong PIN 3 times wipes the vault. Keep your seed phrase backup safe.

### Home menu

Three items — use **UP/DOWN** to highlight, **RIGHT** to enter:

| Item | Screen |
|------|--------|
| Accounts | Browse derived addresses |
| Settings | PIN, autolock, factory reset |
| About | Firmware / version info |

**LEFT** from Home locks the vault immediately.
**Long-press A** (800 ms) from any screen also locks immediately.

### Accounts

- **UP/DOWN** scroll through derived BIP-44 accounts.
- **RIGHT** — open account detail.
  - **UP** from detail → Show xPub (extended public key).
  - **RIGHT** from detail → Sign Message (hold **A** to execute the signing).
  - **LEFT** — go back.

### Settings

Three items cycled with **UP/DOWN**, entered with **RIGHT**:

| Item | Action |
|------|--------|
| Change PIN | Set a new 6-digit PIN (requires current PIN first) |
| Autolock | Cycles: Off → 30 s → 60 s → 5 min → Off |
| Factory Reset | Hold **A** to confirm — wipes vault from storage |

**LEFT** returns to Home.

---

## AkiraKey — TOTP / FIDO2 / Password / SSH Key Manager

AkiraKey is a BLE HID security key. It stores credentials encrypted with a 6-digit PIN
and types them to a paired phone or PC over Bluetooth as a keyboard.

### First launch

The vault is auto-created with PIN **000000**. Change it immediately in Settings → Change PIN.

### Unlock

- **UP/DOWN** change digit, **RIGHT** advance/confirm, **LEFT** step back.

> **Warning:** wrong PIN 3 times wipes the vault entirely.

### Home menu

Five items — **UP/DOWN** to navigate, **RIGHT** to enter, **LEFT** to lock:

| # | Item | Description |
|---|------|-------------|
| 0 | TOTP | Time-based one-time passwords (authenticator codes) |
| 1 | FIDO2 | Hardware security key credentials |
| 2 | Passwords | Username + password pairs |
| 3 | SSH Keys | SSH public key fingerprint |
| 4 | Settings | PIN, BLE, autolock, factory reset |

### TOTP — two-factor authenticator codes

- Up to **16 slots**, each with a label and TOTP secret.
- Live 6-digit codes displayed, refreshed every 500 ms.
- A countdown shows seconds remaining in the 30 s window.
- **UP/DOWN** scroll the list, **RIGHT** to view a code.
- In TOTP view: press **A** or **RIGHT** to type the current code to the BLE-paired device.

### FIDO2 — hardware security key

- Credentials stored per site/service.
- **UP/DOWN** scroll, **RIGHT** to view, **LEFT** to go back.
- In FIDO2 view: hold **A** to approve a WebAuthn challenge from the paired host.

### Passwords

- Stores username + password pairs.
- **UP/DOWN** scroll, **RIGHT** to view, **LEFT** to go back.
- In password view:
  - **A** — type the **username** via BLE HID.
  - **B** — type the **password** via BLE HID.

### SSH Keys

- Displays your SSH public key fingerprint.
- **A** — type the fingerprint as a hex string via BLE HID.

### Settings

Four items — **UP/DOWN** to navigate, **RIGHT** to enter, **LEFT** to go back:

| # | Item | Action |
|---|------|--------|
| 0 | Change PIN | Set a new 6-digit PIN |
| 1 | BLE | Toggle Bluetooth HID on/off |
| 2 | Autolock | Cycle timeout: Off → 30 s → 60 s → 5 min |
| 3 | Factory Reset | Hold **A** to confirm — wipes vault |

### BLE HID pairing

1. Go to Settings → BLE → enable.
2. On your phone/PC open Bluetooth settings and pair with **AkiraKey**.
3. AkiraKey appears as a Bluetooth keyboard.
4. Once paired, pressing **A** on any credential screen types it automatically.

### Lock

- **Long-press B** (2.5 s) from any screen — immediate lock.
- **LEFT** from Home — immediate lock.
- Autolock engages after idle timeout set in Settings.

### RTC / time sync for TOTP

TOTP requires accurate Unix time. On first use:

1. Go to Settings → Set Time.
2. Enter the current Unix timestamp (seconds since 1970-01-01 00:00:00 UTC).
3. AkiraKey saves the time to flash and updates it hourly so it survives reboots.

You can get the current Unix timestamp from any terminal: `date +%s`
