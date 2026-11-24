"""Check TXT reflow anchors and XTC no-op shortcuts using the real readers."""
import argparse
from concurrent.futures import ThreadPoolExecutor
import json
import os
from pathlib import Path
import struct
import subprocess
import time
from PIL import Image
from verify_yacp_simulator_stats import create_fixture
from verify_yacp_fixed_progress import xtc_fixture


def offsets(data):
    assert data[:5] == b'ITXT\x03'
    count = struct.unpack_from('<I', data, 26)[0]
    assert len(data) == 30 + count*4
    return struct.unpack_from('<'+'I'*count, data, 30)


def run_one(program, output, mode):
    output.mkdir(parents=True, exist_ok=False)
    sd = create_fixture(output)
    kind, action = mode.split('-')
    book = sd / ('shortcuts.'+kind)
    if kind == 'txt':
        book.write_text(''.join(f'Line {i:03d}. This text verifies content position while changing font or orientation.\n' for i in range(300)))
    else:
        xtc_fixture(book)
    root = sd / '.crosspoint'
    (root/'recent.json').write_text(json.dumps({'books':[{'path':'/'+book.name,'title':'Format Shortcuts','author':'YACP QA','coverBmpPath':''}]}))
    path = root / 'settings.json'
    values = json.loads(path.read_text())
    values.update(trackReadingStats=0, sideButtonLongPress=4 if action == 'previous' else 3 if action == 'side' else 2,
                  longPressButtonBehavior=3 if action == 'font' else 2 if action == 'front' else 0)
    path.write_text(json.dumps(values))
    key = 'DOWN' if action == 'previous' else 'UP' if action == 'side' else 'RIGHT'
    env = dict(os.environ, SDL_VIDEODRIVER='offscreen', CROSSPOINT_SIM_SD=str(sd),
               CROSSPOINT_SIM_INPUT_SCRIPT=f'1800:CONFIRM:100;5500:DOWN:100;7500:DOWN:100;10500:{key}:2000;16500:BACK:100;19500:QUIT',
               CROSSPOINT_SIM_SCREENSHOTS=f'12000:{output}/held.bmp;15200:{output}/released.bmp')
    with (output/'run.log').open('x') as log:
        proc = subprocess.Popen([str(program)],cwd=output,env=env,stdout=log,stderr=subprocess.STDOUT)
        time.sleep(9)
        if kind == 'txt':
            indices=list(root.glob('txt_*/index.bin'))
            assert len(indices) == 1
            before=indices[0].read_bytes()
            (output/'before-index.bin').write_bytes(before)
        assert proc.wait(timeout=25) == 0
    settings=json.loads(path.read_text())
    progress=list(root.glob('*/progress.bin'))
    assert len(progress) == 1
    page=int.from_bytes(progress[0].read_bytes(),'little')
    expected_page=1 if action=='previous' else 2
    if kind == 'txt' and action != 'previous':
        anchor=offsets(before)[2]
        pages=offsets(indices[0].read_bytes())
        assert pages[page] <= anchor and (page+1 == len(pages) or anchor < pages[page+1]), (mode,page,anchor)
        assert settings['fontSize'] == (14 if action=='font' else 12)
        assert settings['orientation'] == (0 if action=='font' else 3)
    else:
        assert page == expected_page, (mode,page)
        assert settings.get('orientation',0) == 0
        assert settings['fontSize'] == 12
    with Image.open(output/'held.bmp') as a, Image.open(output/'released.bmp') as b:
        assert a.tobytes()==b.tobytes(),mode
        b.save(output/'released.png')
    report={'passed':True,'mode':mode,'page':page,'release_unchanged':True,'hardware_validated':False}
    (output/'result.json').write_text(json.dumps(report,indent=2))
    return report


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('program',type=Path)
    p.add_argument('--output',type=Path,required=True)
    p.add_argument('--mode',nargs='+',default=['txt-font','txt-front','txt-side','txt-previous','xtc-font','xtc-side','xtc-previous'])
    a=p.parse_args()
    with ThreadPoolExecutor(max_workers=3) as pool:
        print(json.dumps(list(pool.map(lambda mode:run_one(a.program.resolve(strict=True),a.output.absolute()/mode,mode),a.mode))))
