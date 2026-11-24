"""Check mode switching, locked controls and preserved Classic preferences on synthetic SDs."""
import argparse
import json
import os
from pathlib import Path
import socket
import subprocess
import time
from urllib.error import HTTPError, URLError
from urllib.request import Request, urlopen

from verify_yacp_simulator_stats import create_fixture
from verify_yacp_home_layout import simulate


def fixture(output, action=None):
    output.mkdir(parents=True, exist_ok=False)
    sd = create_fixture(output)
    path = sd/'.crosspoint/settings.json'
    values = json.loads(path.read_text())
    values.update(refreshFrequency=6, textAntiAliasing=1, trackReadingStats=0)
    if action is not None:
        values['refreshAction'] = action
    path.write_text(json.dumps(values))
    return sd


def ui(program, output):
    sd = fixture(output, 0)
    start = ['1800:BACK:100'] + [f'{2600+i*500}:DOWN:100' for i in range(4)]
    start += ['5100:CONFIRM:100', '6000:UP:100', '7000:CONFIRM:100']
    actions = start + ['8000:CONFIRM:100', '9000:UP:100', '10000:CONFIRM:100',
                      '11000:DOWN:100', '12000:CONFIRM:100', '13000:DOWN:100', '14000:CONFIRM:100',
                      '15000:UP:100', '16000:UP:100', '17000:CONFIRM:100', '18000:DOWN:100',
                      '19000:CONFIRM:100', '20000:CONFIRM:100', '21000:UP:100', '22000:BACK:100',
                      '24000:BACK:100', '26000:QUIT']
    simulate(program, output/'capture', sd, actions,
             [(8500,'classic-help'), (9500,'magic-help'), (10500,'magic-locked'),
              (14500,'smoothing-locked'), (19500,'classic-restored'), (22500,'cancelled')])
    saved = json.loads((sd/'.crosspoint/settings.json').read_text())
    assert (saved['refreshAction'], saved['refreshFrequency'], saved['textAntiAliasing']) == (0,6,1), saved

    text = output/'text-style'
    sd = fixture(text, 2)
    actions = ['1800:BACK:100'] + [f'{2600+i*500}:DOWN:100' for i in range(4)]
    actions += ['5100:CONFIRM:100','6000:CONFIRM:100','7000:DOWN:100','8000:CONFIRM:100',
                '9000:CONFIRM:100','10000:CONFIRM:100','11000:CONFIRM:100','12000:CONFIRM:100',
                '13000:DOWN:100','14000:DOWN:100','15000:DOWN:100','16000:DOWN:100',
                '17000:CONFIRM:100','19000:QUIT']
    simulate(program,text/'capture',sd,actions,[(16500,'aa-disabled'),(18000,'aa-still-disabled')])
    saved = json.loads((sd/'.crosspoint/settings.json').read_text())
    assert saved['textAntiAliasing'] == 1, 'Locked AA changed the saved Classic preference'
    assert saved['refreshAction'] == 2


def api(program, output, board):
    sd = fixture(output, None if board == 'x3' else 2)
    (sd/'.crosspoint/wifi.json').write_text(json.dumps({'lastConnectedSsid':'LocalQA','credentials':[
        {'ssid':'LocalQA','password_obf':'','password_len':0,'password_crc32':0}]}))
    with socket.socket() as reservation:
        reservation.bind(('127.0.0.1',0))
        port = reservation.getsockname()[1]
    base = f'http://127.0.0.1:{port}'
    def get():
        with urlopen(base+'/api/settings',timeout=2) as response:
            return {s['key']:s for s in json.load(response)}
    def post(payload, expected=200):
        request = Request(base+'/api/settings',data=json.dumps(payload).encode(),method='POST',
                          headers={'Content-Type':'application/json'})
        try: response = urlopen(request,timeout=3)
        except HTTPError as error: response = error
        with response: assert response.status == expected,(payload,response.status,response.read())
    phases = ('initial','reload') if board == 'x3' else ('initial',)
    for phase in phases:
        enter = '1800:BACK:100' if board == 'x3' else '1800:TAP:240,756'
        actions = [enter,'2600:DOWN:100','3000:DOWN:100','3400:DOWN:100',
                   '4200:CONFIRM:100','6000:CONFIRM:100','22000:QUIT']
        env = dict(os.environ,SDL_VIDEODRIVER='offscreen',CROSSPOINT_SIM_SD=str(sd),
                   CROSSPOINT_SIM_HTTP_PORT=str(port),CROSSPOINT_SIM_WIFI_NETWORKS='LocalQA:-40:open',
                   CROSSPOINT_SIM_INPUT_SCRIPT=';'.join(actions))
        with (output/f'{phase}.log').open('x') as log:
            process = subprocess.Popen([str(program)],cwd=output,env=env,stdout=log,stderr=subprocess.STDOUT)
            try:
                deadline = time.monotonic()+18
                while True:
                    try:
                        state = get()
                        break
                    except (URLError,TimeoutError):
                        if process.poll() is not None or time.monotonic()>deadline:
                            raise RuntimeError('Settings API did not start')
                        time.sleep(.2)
                assert 'refreshAction' not in state
                if board == 'pro':
                    assert state['displayMode']['value'] == 1
                    assert not state['displayMode']['magicFlashAvailable']
                    assert state['displayMode']['disabledOptions'] == [0]
                    assert state['displayMode']['availabilityHint'] == 'Not yet available for this device.'
                    post({'displayMode':0},400)
                    post({'displayMode':1})
                    state=get()
                    assert (state['refreshFrequency']['value'],state['textAntiAliasing']['value']) == (6,1)
                else:
                    assert state['displayMode']['value']==0
                    assert state['refreshFrequency']['value']==0
                    assert state['textAntiAliasing']['value']==0
                    assert state['refreshFrequency']['disabled'] and state['textAntiAliasing']['disabled']
                    expected=(6,1) if phase=='initial' else (4,0)
                    assert (state['refreshFrequency']['classicValue'],state['textAntiAliasing']['classicValue'])==expected
                    post({'refreshFrequency':2,'textAntiAliasing':1})
                    post({'displayMode':1})
                    state=get()
                    assert (state['refreshFrequency']['value'],state['textAntiAliasing']['value'])==expected
                    assert not state['refreshFrequency']['disabled'] and not state['textAntiAliasing']['disabled']
                    post({'refreshFrequency':4,'textAntiAliasing':0})
                    post({'displayMode':0,'refreshFrequency':3,'textAntiAliasing':1})
                    state=get()
                    assert (state['refreshFrequency']['classicValue'],state['textAntiAliasing']['classicValue'])==(4,0)
                    post({'displayMode':2,'textAntiAliasing':1},400)
                    assert get()['displayMode']['value']==0
                assert process.wait(timeout=25)==0
            finally:
                if process.poll() is None:
                    process.terminate()
                    process.wait(timeout=5)
    return {'passed':True,'board':board,'phases':len(phases),'hardware_validated':False}


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('program',type=Path)
    p.add_argument('--output',type=Path,required=True)
    p.add_argument('--board',choices=('x3','pro'),required=True)
    p.add_argument('--ui',action='store_true')
    a=p.parse_args()
    program=a.program.resolve(strict=True)
    output=a.output.absolute()
    if a.ui:
        assert a.board=='x3'
        ui(program,output)
        result={'passed':True,'ui':True,'hardware_validated':False}
    else: result=api(program,output,a.board)
    (output/'result.json').write_text(json.dumps(result,indent=2))
    print(json.dumps(result))
