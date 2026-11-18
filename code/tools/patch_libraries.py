"""PlatformIO pre-build script: fixes two bugs in STM32duinoBLE 1.5.0.

1. Pairing. ATTClass::getPeerEncrptingConnectionHandle() returns
   ATT_MAX_PEERS + 1 (9) for "no peer is pairing", and HCI.cpp treats any
   handle above ATT_MAX_PEERS as that failure. The STM32WB controller hands out
   connection handles from 0x0801, so every real handle was taken for the
   failure: the watch never sent its public key and LE Secure Connections
   pairing stalled until the phone gave up. The sentinel becomes 0xFFFF, which
   is not a valid connection handle.

2. Large writes. Packets from the radio core are copied into a 128-byte
   buffer (BLE_MODULE_SHARED_MEM_BUFFER_SIZE) and anything that does not fit
   is silently dropped. The controller allows 251-byte packets, so the ATT MTU
   is 242 and a phone with data length extension sends a firmware chunk as one
   ~250-byte packet: it vanished, the write was never answered and the phone's
   firmware transfer timed out on the first chunk. Long protocol messages were
   at risk too. The buffer becomes 1024 bytes: several full packets.

The library lives in .pio/libdeps (not in git), so the fixes are applied there
before each build. It is idempotent and fails the build if the library text
is not what it expects, for example after a version change.
"""
from pathlib import Path

Import("env")  # noqa: F821  (provided by PlatformIO)

EDITS = {
    "ATT.cpp": [("  return ATT_MAX_PEERS + 1;\n}\n// Get the encryption state",
                 "  return 0xFFFF;\n}\n// Get the encryption state", 1)],
    "HCI.cpp": [("if(connectionHandle>ATT_MAX_PEERS){",
                 "if(connectionHandle==0xFFFF){", 2)],
    "STM32_WPAN/app_conf_default.h": [
        ("#define BLE_MODULE_SHARED_MEM_BUFFER_SIZE 128\n",
         "#define BLE_MODULE_SHARED_MEM_BUFFER_SIZE 1024\n", 1)],
}

utility = (Path(env.subst("$PROJECT_LIBDEPS_DIR")) / env.subst("$PIOENV") /  # noqa: F821
           "STM32duinoBLE" / "src" / "utility")
if utility.is_dir():
    for name, edits in EDITS.items():
        path = utility / name
        text = path.read_bytes().decode("utf8")
        changed = False
        for old, new, count in edits:
            if text.count(new) == count and old not in text:
                continue
            if text.count(old) != count:
                raise SystemExit(
                    f"patch_libraries.py: {name} does not match STM32duinoBLE "
                    "1.5.0; check whether the fixes are still needed.")
            text = text.replace(old, new)
            changed = True
            print(f"patch_libraries.py: patched {name}")
        if changed:
            path.write_bytes(text.encode("utf8"))
