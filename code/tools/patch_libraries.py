"""PlatformIO pre-build script: fixes a pairing bug in STM32duinoBLE 1.4.2 and 1.5.0.

ATTClass::getPeerEncrptingConnectionHandle() returns ATT_MAX_PEERS + 1 (9)
for "no peer is pairing", and HCI.cpp treats any handle above ATT_MAX_PEERS
as that failure. The STM32WB controller hands out connection handles from
0x0801, so every real handle was taken for the failure: the watch never sent
its public key and LE Secure Connections pairing stalled until the phone gave
up. The sentinel becomes 0xFFFF, which is not a valid connection handle.

The library lives in .pio/libdeps (not in git), so the fix is applied there
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
}

utility = (Path(env.subst("$PROJECT_LIBDEPS_DIR")) / env.subst("$PIOENV") /  # noqa: F821
           "STM32duinoBLE" / "src" / "utility")
if utility.is_dir():
    for name, edits in EDITS.items():
        path = utility / name
        text = path.read_bytes().decode("utf8")
        for old, new, count in edits:
            if text.count(new) == count and old not in text:
                continue
            if text.count(old) != count:
                raise SystemExit(
                    f"patch_libraries.py: {name} does not match STM32duinoBLE "
                    "1.4.2; check whether the pairing fix is still needed.")
            text = text.replace(old, new)
            print(f"patch_libraries.py: fixed pairing handle check in {name}")
        path.write_bytes(text.encode("utf8"))
