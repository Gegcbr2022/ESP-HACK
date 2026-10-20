"""Back up and independently verify an ESP32 before flashing it.

Run `python scripts/device_flash.py --help`. The backup is private device data;
it is never put in a release/source archive by this script.
"""
import argparse
import datetime
import hashlib
import json
from pathlib import Path
import re
import subprocess
import struct
import sys


def sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(65536), b""):
            digest.update(block)
    return digest.hexdigest()


def release_segments(manifest_path, flash_bytes):
    manifest_path=Path(manifest_path).resolve()
    manifest=json.loads(manifest_path.read_text(encoding="utf-8"))
    if manifest.get("chip")!="esp32" or manifest.get("display")!="SH1106" or flash_bytes!=4194304:
        raise RuntimeError("Release does not match ESP32 / SH1106 / 4MiB target")
    limits={0x1000:0x7000,0x8000:0x1000,0xE000:0x2000,0x10000:0x1E0000}
    artifacts={}
    for segment in manifest["segments"]:
        offset=int(segment["offset"],0)
        if offset not in limits or offset in artifacts:
            raise RuntimeError("Unexpected or duplicate release segment")
        binary=(manifest_path.parent/segment["file"]).resolve()
        if not binary.is_relative_to(manifest_path.parent):
            raise RuntimeError("Artifact path escapes the release directory")
        size=binary.stat().st_size
        if not size or size>limits[offset] or size!=segment["bytes"] or sha256(binary)!=segment["sha256"]:
            raise RuntimeError("Release segment size or integrity failed: "+str(binary))
        artifacts[offset]=binary
    if set(artifacts)!=set(limits):
        raise RuntimeError("Expected four unique bootloader/partition/OTA/application segments")
    partitions=artifacts[0x8000].read_bytes()
    app_size=0
    for at in range(0,len(partitions)-31,32):
        magic,kind,subtype,offset,size=struct.unpack_from("<HBBII",partitions,at)
        if magic!=0x50AA:break
        if kind==0 and subtype==0x10 and offset==0x10000:app_size=size
    if not app_size or app_size>0x1E0000 or artifacts[0x10000].stat().st_size>app_size:
        raise RuntimeError("Application does not fit the declared app0 partition")
    segments=[]
    for offset in sorted(artifacts):segments.extend([hex(offset),str(artifacts[offset])])
    return segments


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--esptool", type=Path, required=True, help="Trusted esptool.py path")
    parser.add_argument("--port", required=True)
    parser.add_argument("--baud", type=int, default=115200)
    commands = parser.add_subparsers(dest="action", required=True)
    backup = commands.add_parser("backup")
    backup.add_argument("--directory", type=Path, required=True)
    backup.add_argument("--double-read",action="store_true",help="Compare two host dumps instead of independent full-flash device digest verification")
    for name in ("flash", "restore"):
        command = commands.add_parser(name)
        command.add_argument("--backup-receipt", type=Path, required=True)
        if name == "flash":
            command.add_argument("--release-manifest", type=Path, required=True)
    args = parser.parse_args()
    base = [sys.executable, str(args.esptool.resolve()), "--chip", "esp32", "--port", args.port,
            "--baud", str(args.baud)]
    log = []

    def tool(arguments, before="default_reset", after="no_reset"):
        command = base[:1] + ["-u"] + base[1:] + ["--before", before, "--after", after] + list(map(str, arguments))
        process = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                   text=True, errors="replace", bufsize=1)
        chunks=[]
        for output in process.stdout:
            print(output,end="",flush=True);chunks.append(output)
        code=process.wait();output="".join(chunks);log.append(output)
        if code:
            raise RuntimeError("esptool failed: " + str(code))
        return output

    if args.action == "backup":
        directory = args.directory.resolve()
        directory.mkdir(parents=True, exist_ok=True)
        stamp = datetime.datetime.now(datetime.timezone.utc).strftime("%Y%m%dT%H%M%SZ")
        image = directory / ("esp32-device-full-" + stamp + ".bin")
        verification = directory / ("esp32-verification-" + stamp + ".bin")
        receipt_path = image.with_suffix(".json")
        if image.exists() or verification.exists() or receipt_path.exists():
            raise RuntimeError("Backup filenames already exist; no overwrite allowed")
        try:
            first = tool(["read_flash", "0", "ALL", image])
            mac = re.search(r"MAC:\s*([0-9a-fA-F:]{17})", first)
            if not mac:
                raise RuntimeError("Cannot identify device MAC; backup not certified")
            digest = sha256(image)
            if args.double_read:
                tool(["read_flash", "0", "ALL", verification], before="no_reset", after="hard_reset")
                if digest != sha256(verification):
                    raise RuntimeError("Full flash reads differ; retaining both, not allowing flash")
                verification.unlink()
            else:
                # Independently read/hash all flash on the device. This avoids
                # a second long UART transfer while checking the complete dump.
                tool(["verify_flash","0",image],before="no_reset",after="hard_reset")
            receipt = {"schema": 1, "chip": "esp32", "mac": mac.group(1).lower(),
                       "port_at_backup": args.port, "utc": stamp, "file": image.name,
                       "bytes": image.stat().st_size, "sha256": digest,
                       "independent_full_reads":2 if args.double_read else 1,"verified":True,
                       "verification_method":"two-full-host-reads-sha256" if args.double_read else "esptool-full-flash-md5",
                       "device_md5_verified":not args.double_read}
            receipt_path.write_text(json.dumps(receipt, indent=2) + "\n", encoding="utf-8")
            image.with_suffix(".bin.sha256").write_text(digest + "  " + image.name + "\n", encoding="ascii")
            print("VERIFIED DEVICE BACKUP:", image, "SHA256", digest)
        finally:
            image.with_suffix(".log").write_text("\n".join(log), encoding="utf-8")
        return

    receipt_path = args.backup_receipt.resolve()
    receipt = json.loads(receipt_path.read_text(encoding="utf-8"))
    image = receipt_path.parent / receipt["file"]
    image=image.resolve()
    if not image.is_relative_to(receipt_path.parent):raise RuntimeError("Backup path escapes receipt directory")
    if (receipt.get("schema") != 1 or receipt.get("chip") != "esp32" or
            not receipt.get("verified") or not (receipt.get("independent_full_reads")==2 or
                (receipt.get("independent_full_reads")==1 and receipt.get("verification_method")=="esptool-full-flash-md5" and receipt.get("device_md5_verified") is True)) or
            image.stat().st_size != receipt["bytes"] or sha256(image) != receipt["sha256"]):
        raise RuntimeError("Backup receipt or image integrity invalid; refusing write")
    identification = tool(["flash_id"])
    mac = re.search(r"MAC:\s*([0-9a-fA-F:]{17})", identification)
    if not mac or mac.group(1).lower() != receipt["mac"]:
        raise RuntimeError("Attached device does not match the verified backup; refusing write")
    if args.action == "restore":
        segments = ["0x0", str(image)]
        write_options = []
    else:
        segments=release_segments(args.release_manifest,receipt["bytes"])
        write_options = ["--flash_mode", "dio", "--flash_freq", "40m", "--flash_size", "4MB"]
    try:
        # Separate segments leave the 0x9000..0xDFFF NVS region intact.
        tool(["write_flash"] + write_options + segments, before="no_reset")
        tool(["verify_flash"] + segments, before="no_reset", after="hard_reset")
        print("WRITE AND FLASH VERIFICATION PASSED")
    finally:
        receipt_path.with_name(args.action + "-" + receipt_path.stem + ".log").write_text("\n".join(log), encoding="utf-8")


if __name__ == "__main__":
    main()
