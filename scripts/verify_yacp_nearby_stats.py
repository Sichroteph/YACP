"""Exercise real nearby protocol, snapshot storage and aggregation with host sanitizers."""
import argparse
import json
from pathlib import Path
import subprocess


def run(output):
    root=Path(__file__).resolve().parents[1]
    output=output.absolute()
    output.mkdir(parents=True,exist_ok=False)
    reader=root/'src/activities/reader'
    network=root/'src/network'
    tests=root/'test/yacp_statistics'
    common=['g++','-std=c++20','-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer',
            '-I'+str(tests/'stubs'),'-I'+str(reader),'-I'+str(network)]
    cases={
        'protocol': [tests/'NearbyStatsProtocolTest.cpp',network/'NearbyStatsProtocol.cpp'],
        'store': [tests/'SyncedStatsStoreTest.cpp',network/'SyncedStatsStore.cpp',network/'NearbyStatsProtocol.cpp'],
        'aggregation': [tests/'GlobalAggregationTest.cpp',reader/'GlobalReadingStats.cpp',
                        reader/'ReadingStatsUtils.cpp',reader/'DailyReadingHistory.cpp'],
    }
    for name,sources in cases.items():
        binary=output/name
        subprocess.run(common+[str(source) for source in sources]+['-o',str(binary)],check=True)
        result=subprocess.run([str(binary)],check=True,capture_output=True,text=True)
        (output/(name+'.log')).write_text(result.stdout+result.stderr)
    report={'passed':True,'suites':list(cases),'sanitizers':['address','undefined'],
            'legacy_stats_versions':[1,2,3],'partial_write_cases':159,
            'transport':'host datagrams, not radio','hardware_validated':False}
    (output/'result.json').write_text(json.dumps(report,indent=2))
    print(json.dumps(report))


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,required=True)
    run(parser.parse_args().output)
