"""Install a Lua SD app over the existing USB UART; leave the device on its main menu."""
import argparse
import hashlib
from pathlib import Path
import re
import time
import serial

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("file",type=Path)
    parser.add_argument("--port",required=True)
    parser.add_argument("--name")
    args=parser.parse_args()
    name=args.name or args.file.name
    if not re.fullmatch(r"[A-Za-z0-9_-]{1,36}\.lua",name):parser.error("Name must be a safe ASCII name ending in .lua (maximum40 characters)")
    data=args.file.read_bytes()
    if not 1<=len(data)<=32768:parser.error("Source must contain1..32768 bytes")
    digest=hashlib.sha256(data).hexdigest()
    device=serial.Serial(port=None,baudrate=115200,timeout=0.5,write_timeout=10)
    device.dtr=False;device.rts=False;device.port=args.port
    with device:
        device.reset_input_buffer()
        device.write(f"APP_PUT {name} {len(data)} {digest}\n".encode("ascii"));device.flush()
        deadline=time.monotonic()+20;ready=False
        while time.monotonic()<deadline:
            reply=device.readline().decode("utf-8",errors="replace").strip()
            if reply.startswith("APP_ERROR"):raise RuntimeError(reply)
            if reply==f"APP_READY {len(data)}":ready=True;break
        if not ready:raise RuntimeError("Device did not accept upload; return to the main menu and check SD")
        for at in range(0,len(data),256):device.write(data[at:at+256])
        device.flush();deadline=time.monotonic()+20
        while time.monotonic()<deadline:
            reply=device.readline().decode("utf-8",errors="replace").strip()
            if reply.startswith("APP_ERROR"):raise RuntimeError(reply)
            if reply==f"APP_INSTALLED {name} {len(data)} {digest}":print(reply);return
        raise RuntimeError("No confirmed installation; inspect SD/device error")

if __name__=="__main__":main()
