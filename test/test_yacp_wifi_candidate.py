import base64
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
from prepare_yacp_migration import prepare as copy_snapshot, enumerate_files
from prepare_yacp_wifi_candidate import prepare, mac_bytes
from yacp_credential_migration import fnv1a, xor_bytes

SOURCE = bytes.fromhex('102030405060')
TARGET = bytes.fromhex('708090a0b0c0')


class WifiCandidateTest(unittest.TestCase):
    def setUp(self):
        # Synthetic fixtures remain available for review. No host deletion.
        parent = ROOT / 'qa-artifacts/wifi-candidate-tests'
        parent.mkdir(parents=True, exist_ok=True)
        self.root = Path(tempfile.mkdtemp(prefix='case-', dir=parent))
        source = self.root / 'source'
        source.mkdir()
        password = b'synthetic-password'
        payload = b'CPV1' + struct.pack('<I', fnv1a(SOURCE + password)) + password
        data = {'lastConnectedSsid': 'QA', 'credentials': [{'ssid': 'QA',
                'password_obf': base64.b64encode(xor_bytes(payload, SOURCE)).decode()}]}
        (source / 'wifi.json').write_text(json.dumps(data), encoding='utf-8')
        self.snapshot = self.root / 'snapshot'
        copy_snapshot(source, self.snapshot, 'tiny')

    def test_repeat_and_each_interrupted_output_resume_without_source_changes(self):
        source_before = enumerate_files(self.snapshot)
        complete = self.root / 'complete'
        result = prepare(self.snapshot, complete, SOURCE, TARGET)
        before = {p.name: p.read_bytes() for p in complete.iterdir()}
        self.assertEqual(prepare(self.snapshot, complete, SOURCE, TARGET), result)
        self.assertFalse(result['same_device_target'])
        self.assertFalse(result['ready_to_activate'])
        for index, name in enumerate(('SOURCE.json', 'candidate-wifi.json', 'verification.json')):
            resumed = self.root / f'resume-{index}'
            resumed.mkdir()
            for previous in list(before)[:index]:
                (resumed / previous).write_bytes(before[previous])
            # Do not rely on directory enumeration order for the origin guard.
            if name != 'SOURCE.json': (resumed / 'SOURCE.json').write_bytes(before['SOURCE.json'])
            (resumed / name).write_bytes(before[name][:19])
            prepare(self.snapshot, resumed, SOURCE, TARGET)
            self.assertEqual(before, {p.name: p.read_bytes() for p in resumed.iterdir()})
        self.assertEqual(source_before, enumerate_files(self.snapshot))

    def test_wrong_identity_and_changed_snapshot_create_nothing(self):
        output = self.root / 'output'
        with self.assertRaises(ValueError): prepare(self.snapshot, output, TARGET, TARGET)
        self.assertFalse(output.exists())
        (self.snapshot / 'original/wifi.json').write_text('{}')
        with self.assertRaises(ValueError): prepare(self.snapshot, output, SOURCE, SOURCE)
        self.assertFalse(output.exists())

    def test_existing_conflicting_candidate_and_identity_are_preserved(self):
        output = self.root / 'output'
        prepare(self.snapshot, output, SOURCE, SOURCE)
        before = enumerate_files(output)
        with self.assertRaises(ValueError): prepare(self.snapshot, output, SOURCE, TARGET)
        self.assertEqual(before, enumerate_files(output))
        (output / 'candidate-wifi.json').write_bytes(b'foreign contents')
        before = enumerate_files(output)
        with self.assertRaises(ValueError): prepare(self.snapshot, output, SOURCE, SOURCE)
        self.assertEqual(before, enumerate_files(output))

    def test_overlap_and_nextcloud_are_rejected(self):
        for output in (self.snapshot, self.snapshot / 'experiment', self.root / 'Nextcloud/output'):
            with self.assertRaises(ValueError): prepare(self.snapshot, output, SOURCE, SOURCE)

    def test_mac_notation_is_explicit_and_unambiguous(self):
        for value in ('102030405060', '10:20:30:40:50:60', '10-20-30-40-50-60'):
            self.assertEqual(mac_bytes(value), SOURCE)
        for value in ('10:20-30:40:50:60', '', '10203040506070', '1020304050xx', None):
            with self.assertRaises(ValueError): mac_bytes(value)


if __name__ == '__main__':
    unittest.main()
