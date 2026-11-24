"""Compare actual bitmap decoding with preserved CrossPoint and YACP host probes."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess


def bmp(width, height, bpp, low=25, high=230, top_down=False):
    colors = 1 << bpp if bpp <= 8 else 0
    palette = bytes()
    if colors:
        levels = [round(255 * i / (colors - 1)) for i in range(colors)] if bpp <= 2 else [round(low + (high-low) * i/(colors-1)) for i in range(colors)]
        palette = b"".join(bytes((value, value, value, 0)) for value in levels)
    row_size = (width*bpp+31)//32*4
    pixels = bytearray()
    for y in range(height):
        row = bytearray(row_size)
        for x in range(width):
            index = (x+y*3) % (colors or 256)
            value = round(low+(high-low)*((x+y*3)%19)/18)
            if bpp>=24:
                row[x*(bpp//8):x*(bpp//8)+3] = bytes((value,value,value))
                if bpp==32: row[x*4+3]=255
            elif bpp==8: row[x]=index
            else: row[x*bpp//8] |= index << (8-bpp-(x*bpp)%8)
        pixels.extend(row)
    offset=54+len(palette)
    return struct.pack("<2sIHHI", b"BM",offset+len(pixels),0,0,offset)+struct.pack("<IiiHHIIiiII",
        40,width,-height if top_down else height,1,bpp,0,len(pixels),0,0,colors,0)+palette+pixels


def probe(program, source, mode):
    result = subprocess.run([str(program),str(source),mode], capture_output=True, text=True)
    if result.returncode:
        raise AssertionError((str(program),source.name,mode,result.returncode,result.stderr))
    return bytes.fromhex(result.stdout.strip())


def run(current, crosspoint, original, output):
    programs=[p.resolve(strict=True) for p in (current,crosspoint,original)]
    current,crosspoint,original=programs
    output=output.absolute();output.mkdir(parents=True,exist_ok=False)
    results=[]
    cases=[(f"bpp{bits}",9,7,bits,25,230,False) for bits in (1,2,4,8,24,32)]
    cases += [("top-down",11,5,24,25,230,True),("tiny",1,1,24,80,80,False),
              ("flat",13,8,24,128,128,False),("narrow-range",19,5,24,100,140,False),
              ("light",17,6,32,120,250,False)]
    for name,w,h,bits,low,high,top_down in cases:
        path=output/(name+".bmp");path.write_bytes(bmp(w,h,bits,low,high,top_down))
        result={"case":name}
        for mode in ("legacy","native","adaptive"):
            expected=probe(original if mode=="adaptive" else crosspoint,path,mode)
            actual=probe(current,path,mode)
            assert expected==actual,(name,mode,"pixel output differs")
            result[mode]=hashlib.sha256(actual).hexdigest()
        results.append(result)
    assert any(r["adaptive"]!=r["native"] for r in results)
    report={"passed":True,"cases":results,"default_matches_crosspoint":True,
            "adaptive_matches_yacp":True,"sanitizers":["address","undefined"],"hardware_validated":False}
    (output/"result.json").write_text(json.dumps(report,indent=2),encoding="utf-8")
    print(json.dumps({k:v for k,v in report.items() if k!="cases"}))


if __name__=="__main__":
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("current",type=Path);parser.add_argument("crosspoint",type=Path);parser.add_argument("original",type=Path)
    parser.add_argument("--output",type=Path,required=True)
    args=parser.parse_args();run(args.current,args.crosspoint,args.original,args.output)
