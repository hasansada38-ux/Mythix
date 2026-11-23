import glob
import os
import struct
import sys
import zipfile

build_output = sys.argv[1]
packages = glob.glob(os.path.join(build_output, "**", "*.geode"), recursive=True)
if not packages:
    raise SystemExit(f"No .geode package found under {build_output}")

for package in packages:
    with zipfile.ZipFile(package) as archive:
        names = archive.namelist()
        if "logo.png" not in names:
            raise SystemExit(f"{os.path.basename(package)} is missing root-level logo.png")
        data = archive.read("logo.png")
        if not data.startswith(b"\x89PNG\r\n\x1a\n"):
            raise SystemExit(f"{os.path.basename(package)} contains an invalid logo.png (bad PNG signature)")
        if len(data) < 24:
            raise SystemExit(f"{os.path.basename(package)} contains a truncated logo.png")
        width, height = struct.unpack(">II", data[16:24])
        if width != 512 or height != 512:
            raise SystemExit(f"{os.path.basename(package)} logo.png is {width}x{height}; expected 512x512")
        print(f"Verified {os.path.basename(package)}: root logo.png is valid ({width}x{height}, {len(data)} bytes)")
