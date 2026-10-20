"""Make a lossless nearest-neighbor PNG contact sheet from test PGM snapshots."""
import argparse
from pathlib import Path
import struct
import zlib

parser=argparse.ArgumentParser()
parser.add_argument("snapshots",type=Path)
parser.add_argument("output",type=Path)
args=parser.parse_args()
scale,gap=4,16
width,height=3*128*scale+4*gap,3*64*scale+4*gap
pixels=bytearray([40])*(width*height)
names=[f"pocket_{index}.pgm" for index in range(8)]+["pocket_no_sd.pgm"]
for index,name in enumerate(names):
    source=(args.snapshots/name).read_bytes()
    header=b"P5\n128 64\n255\n"
    if not source.startswith(header) or len(source)!=len(header)+128*64:
        raise ValueError("Unexpected PGM dimensions: "+name)
    source=source[len(header):]
    x0,y0=gap+(index%3)*(128*scale+gap),gap+(index//3)*(64*scale+gap)
    for y in range(64*scale):
        at=(y0+y)*width+x0
        for x in range(128*scale):pixels[at+x]=source[(y//scale)*128+x//scale]

def chunk(kind,data):
    return struct.pack(">I",len(data))+kind+data+struct.pack(">I",zlib.crc32(kind+data)&0xFFFFFFFF)

rows=b"".join(b"\0"+pixels[y*width:(y+1)*width] for y in range(height))
png=b"\x89PNG\r\n\x1a\n"+chunk(b"IHDR",struct.pack(">IIBBBBB",width,height,8,0,0,0,0))
png+=chunk(b"IDAT",zlib.compress(rows,9))+chunk(b"IEND",b"")
args.output.parent.mkdir(parents=True,exist_ok=True)
args.output.write_bytes(png)
print(args.output)
