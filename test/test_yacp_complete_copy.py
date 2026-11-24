"""Fault and data-preservation tests. Fixtures are retained, never deleted."""
import base64
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'scripts'))
from migrate_yacp_copy import build_plan, materialize, text_progress, verified_snapshot, local_output
from prepare_yacp_migration import enumerate_files
from yacp_data_formats import esp32_book_path,fnv64_book_path
from yacp_credential_migration import fnv1a,xor_bytes
from test_yacp_bookmark_migration import epub,legacy


class Interrupted(RuntimeError):pass


class CompleteCopyTest(unittest.TestCase):
    def setUp(self):
        self.folder=Path(tempfile.mkdtemp(prefix='complete-copy-',dir=ROOT/'qa-artifacts'))
        self.snapshot=self.folder/'snapshot'
        self.original=self.snapshot/'original'
        self.original.mkdir(parents=True)
        self.cache=fnv64_book_path('/Book.epub')
        self.key=bytes.fromhex('010203040506')
        envelope=b'CPV1'+struct.pack('<I',fnv1a(self.key+b'example'))+b'example'
        password=base64.b64encode(xor_bytes(envelope,self.key)).decode()
        self.files={
            'Book.epub':epub().getvalue(),
            '.sleep/image.bmp':b'Original sleep image bytes',
            '.crosspoint/crossink-settings.json':json.dumps({'fontFamily':0,'fontSize':1,'unknownFutureField':42}).encode(),
            '.crosspoint/settings.json':b'{"fontSize":250}',
            '.crosspoint/state.json':json.dumps({'openEpubPath':'/Book.epub','recentSleepImages':[9,3],
                                               'recentSleepPos':1,'recentSleepFill':2}).encode(),
            '.crosspoint/recent.json':b'{"books":[{"path":"/Book.epub","title":"Book","author":"Author","coverBmpPath":"/old.bmp"}]}',
            f'.crosspoint/{self.cache}/progress.bin':struct.pack('<HHH',0,0,2),
            f'.crosspoint/{self.cache}/stats.bin':struct.pack('<BHII',1,3,456,9),
            f'.crosspoint/{self.cache}/future-data.bin':b'Keep unknown data',
            '.crosspoint/bookmarks/book.bin':legacy(5),
            '.crosspoint/global_stats.bin':struct.pack('<BIII',1,2,345,12),
            '.crosspoint/unknown-store.bin':b'future data',
            '.crosspoint/wifi.json':json.dumps({'credentials':[{'ssid':'Synthetic','password_obf':password}],
                                              'lastConnectedSsid':'Synthetic'}).encode(),
            '.crosspoint/koreader.json':json.dumps({'username':'test','password_obf':password,'serverUrl':'',
                                                  'matchMethod':1,'syncBehavior':1}).encode(),
        }
        self.refresh()

    def refresh(self):
        for name,data in self.files.items():
            path=self.original/name
            path.parent.mkdir(parents=True,exist_ok=True)
            path.write_bytes(data)
        self.manifest={'source_variant':'tiny','files':enumerate_files(self.original)}
        (self.snapshot/'source-manifest.json').write_text(json.dumps(self.manifest))
        (self.snapshot/'COPY-VERIFIED.json').write_text('{"copy_verified":true,"source_variant":"tiny"}')
        self.plan=build_plan(self.snapshot,self.manifest,self.key,self.key)

    def run_copy(self,name='result',fault=None):
        return materialize(self.snapshot,self.folder/name,self.manifest,self.plan,fault)

    def test_complete_repeat_retains_every_original_and_bookmark(self):
        self.assertEqual(verified_snapshot(self.snapshot),self.manifest)
        result=self.run_copy()
        before=enumerate_files(self.folder/'result')
        self.assertEqual(result,self.run_copy())
        self.assertEqual(before,enumerate_files(self.folder/'result'))
        card=self.folder/'result/card'
        for name,data in self.files.items():
            destination=('.yacp-migration/original-crosspoint/'+name[len('.crosspoint/'):]
                         if name.startswith('.crosspoint/') else name)
            self.assertEqual((card/destination).read_bytes(),data)
        state=json.loads((card/'.crosspoint/state.json').read_text())
        self.assertEqual(state['openEpubPath'],'/Book.epub')
        self.assertEqual(state['recentSleepFill'],0)
        self.assertEqual(json.loads((card/'.crosspoint/bookmarks/Book.json').read_text())['bookmarks'][0]['vo'],len('Earlier sentence'))
        settings=json.loads((card/'.crosspoint/settings.json').read_text())
        self.assertEqual(settings['fontSize'],12)
        self.assertNotIn('unknownFutureField',settings)
        sync=json.loads((card/'.crosspoint/koreader.json').read_text())
        self.assertEqual(sync['serverUrl'],'https://sync.koreader.rocks:443')
        self.assertEqual(sync['syncBehavior'],1)
        self.assertEqual(xor_bytes(base64.b64decode(sync['password_obf']),self.key),b'example')

    def test_every_durable_boundary_resumes_without_source_changes(self):
        events=[]
        self.run_copy('baseline',events.append)
        expected=enumerate_files(self.folder/'baseline/card')
        before=enumerate_files(self.original)
        for n,event in enumerate(events):
            def stop(current):
                if current==event:raise Interrupted()
            with self.assertRaises(Interrupted):self.run_copy(str(n),stop)
            self.run_copy(str(n))
            self.assertEqual(expected,enumerate_files(self.folder/str(n)/'card'))
        self.assertEqual(before,enumerate_files(self.original))

    def test_torn_manifest_source_and_generated_files_resume(self):
        def stop(event):
            if event=='verified':raise Interrupted()
        with self.assertRaises(Interrupted):self.run_copy(fault=stop)
        pending=self.folder/'result/card.pending'
        for path in (pending/'Book.epub',pending/'.crosspoint/settings.json',self.folder/'result/intent.json'):
            path.write_bytes(path.read_bytes()[:7])
        self.run_copy()
        self.assertTrue((self.folder/'result/COMPLETE.json').exists())

    def test_changed_or_foreign_destinations_are_not_overwritten(self):
        self.run_copy()
        file=self.folder/'result/card/.crosspoint/state.json'
        file.write_bytes(b'user changed this')
        before=enumerate_files(self.folder/'result')
        with self.assertRaises(ValueError):self.run_copy()
        self.assertEqual(before,enumerate_files(self.folder/'result'))
        foreign=self.folder/'foreign'
        foreign.mkdir()
        (foreign/'precious').write_bytes(b'keep')
        with self.assertRaises(ValueError):self.run_copy('foreign')
        self.assertEqual((foreign/'precious').read_bytes(),b'keep')

    def test_unresolved_progress_disables_auto_resume_and_retains_bytes(self):
        self.files[f'.crosspoint/{self.cache}/progress.bin']=struct.pack('<HHH',0,1,2)
        self.refresh()
        self.run_copy()
        card=self.folder/'result/card'
        self.assertFalse((card/f'.crosspoint/{esp32_book_path("/Book.epub")}/progress.bin').exists())
        self.assertEqual(json.loads((card/'.crosspoint/state.json').read_text())['openEpubPath'],'')
        self.assertTrue(any(r['reason']=='exact_reading_position_requires_review' for r in self.plan.report['review']))

    def test_cache_collision_requires_explicit_verified_origin(self):
        old=esp32_book_path('/Book.epub')
        self.files[f'.crosspoint/{old}/progress.bin']=struct.pack('<HHH',0,1,2)
        self.refresh()
        self.assertEqual(self.plan.report['migrated_positions'],0)
        selected=build_plan(self.snapshot,self.manifest,self.key,self.key,source_cache_policy='yacp-fnv64')
        self.assertEqual(selected.report['migrated_positions'],1)
        self.assertEqual(selected.report['archived_inactive_caches'],[old])

    def test_bad_credentials_and_future_statistics_are_quarantined(self):
        self.files['.crosspoint/global_stats.bin']=b'\xffUnknown'
        self.refresh()
        plan=build_plan(self.snapshot,self.manifest,b'000000',self.key)
        self.assertNotIn('.crosspoint/wifi.json',plan.files)
        self.assertNotIn('.crosspoint/global_stats.bin',plan.files)
        self.assertIn('.yacp-migration/original-crosspoint/global_stats.bin',plan.files)

    def test_snapshot_mutation_and_unsafe_destinations_rejected(self):
        (self.original/'Book.epub').write_bytes(b'changed')
        with self.assertRaises(ValueError):verified_snapshot(self.snapshot)
        for path in (self.snapshot,self.snapshot/'inside',self.folder/'Nextcloud'/'output'):
            with self.assertRaises(ValueError):local_output(path,self.snapshot)

    def test_source_change_during_copy_prevents_publication(self):
        def change(event):
            if event=='file:Book.epub':
                (self.original/'Book.epub').write_bytes(b'changed during copy')
        with self.assertRaises(ValueError):self.run_copy(fault=change)
        self.assertFalse((self.folder/'result/card').exists())
        self.assertFalse((self.folder/'result/COMPLETE.json').exists())

    def test_flattened_bookmark_collision_keeps_both_originals(self):
        for path in ('/A/Book.epub','/A_Book.epub'):
            self.files[path.lstrip('/')]=epub().getvalue()
            name='nested' if '/' in path[1:] else 'flat'
            self.files[f'.crosspoint/bookmarks/{name}.bin']=legacy(5,path=path)
        self.refresh()
        self.assertNotIn('.crosspoint/bookmarks/A_Book.json',self.plan.files)
        self.assertEqual(sum(r['reason']=='bookmarks_require_review' for r in self.plan.report['review']),2)

    def test_bad_epub_is_preserved_without_blocking_other_stores(self):
        self.files['Book.epub']=b'Not a ZIP'
        self.refresh()
        self.assertEqual(self.plan.report['migrated_positions'],0)
        self.assertIn('.crosspoint/global_stats.bin',self.plan.files)
        self.run_copy()

    def test_fixed_format_positions_are_mapped_with_bounds(self):
        from verify_yacp_fixed_progress import xtc_fixture
        xtc=self.folder/'fixture.xtc'
        xtc_fixture(xtc)
        self.files['Sample.xtc']=xtc.read_bytes()
        self.files['Sample.txt']=b'First line\nSecond line\n'
        self.files['.crosspoint/'+esp32_book_path('/Sample.xtc').replace('epub_','xtc_')+'/progress.bin']=struct.pack('<I',2)
        self.files['.crosspoint/'+esp32_book_path('/Sample.txt').replace('epub_','txt_')+'/progress.bin']=struct.pack('<HI',1,11)
        self.refresh()
        self.assertEqual(self.plan.report['migrated_positions'],3)
        self.assertEqual(sum(b.get('format') in ('txt','xtc') for b in self.plan.report['books']),2)

    def test_text_offset_and_legacy_index_conversion(self):
        record=struct.pack('<HI',2,37)
        self.assertEqual(text_progress(record,100),record)
        index=bytearray(30)
        index[:5]=b'ITXT\x03'
        struct.pack_into('<I',index,5,100)
        struct.pack_into('<I',index,26,3)
        index+=struct.pack('<III',0,15,37)
        self.assertEqual(text_progress(struct.pack('<HH',2,0),100,index),record)
        for data,size,cache in [(record,20,None),(b'\x01\0\0\0',100,None),(record[:5],100,None),
                                (b'\x02\0\0\0',101,index)]:
            with self.assertRaises(ValueError):text_progress(data,size,cache)


if __name__=='__main__':unittest.main()
