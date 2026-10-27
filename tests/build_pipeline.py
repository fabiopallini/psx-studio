"""Optional developer tests of the native pipeline; Python is not used by builds."""
import contextlib
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
EDITOR = ROOT / 'build' / ('psx-studio.exe' if os.name == 'nt' else 'psx-studio')


@contextlib.contextmanager
def build_lock(path):
    # Interoperate with the native pipeline's OS lock from a second process.
    with path.open('a+b') as lock:
        if os.name == 'nt':
            import msvcrt
            lock.seek(0)
            msvcrt.locking(lock.fileno(), msvcrt.LK_NBLCK, 1)
        else:
            import fcntl
            fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        yield


class BuildTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='pipeline-test-', dir=ROOT / 'build')
        self.root = Path(self.temp.name)
        shutil.copytree(ROOT / 'pipeline', self.root / 'pipeline')
        shutil.copytree(ROOT / 'runtime', self.root / 'runtime')
        self.sdk = self.root / 'sdk'
        for name in ('psyq', 'include', 'lib'):
            (self.sdk / name).mkdir(parents=True, exist_ok=True)
        for name in ('CCPSX.EXE', 'PSYMAKE.EXE'):
            (self.sdk / 'psyq' / name).touch()
        self.project = self.root / 'game.psx'
        # Keep the output-management fixture independent of demo assets/Scenes.
        self.data = {
            'version': 1, 'name': 'Pipeline fixture', 'psyq_path': '',
            'camera': [0, 0, -600],
            'objects': [{'id': 1, 'name': 'Cube', 'kind': 'mesh',
                         'position': [0, 24, 0], 'rotation': [0, 0, 0],
                         'scale': [1, 1, 1], 'bounds': [24, 24, 24],
                         'solid': True, 'asset': '', 'module': ''}],
        }
        self.save()

    def tearDown(self):
        self.temp.cleanup()

    def save(self):
        self.project.write_text(json.dumps(self.data))

    def prepare(self):
        result = subprocess.run([str(EDITOR), '--build-ps1', '--project', str(self.project),
                                 '--psyq', str(self.sdk), '--root', str(self.root), '--prepare-only'],
                                capture_output=True, text=True, encoding='utf-8')
        if result.returncode:
            raise RuntimeError(result.stdout + result.stderr)

    def output(self):
        paths = list((self.root / 'build/ps1').iterdir())
        self.assertEqual(len(paths), 1)
        return paths[0]

    def test_same_project_reuses_directory_and_refreshes_snapshot(self):
        self.prepare()
        output = self.output()
        self.assertEqual(output, self.root / 'build/ps1/game')
        self.data['objects'][0]['position'][0] = 77
        self.save()
        self.prepare()
        self.assertEqual(output, self.output())
        self.assertEqual(json.loads((output / 'project.snapshot.psx').read_text()), self.data)
        self.assertTrue((output / 'src/scene.c').is_file())
        self.assertIn('@sources.rsp', (output / 'src/makefile.mak').read_text())
        self.assertIn('scene.c', (output / 'src/sources.rsp').read_text())
        self.assertTrue((output / 'src/camera.h').read_text().startswith('#define PSX_RUNTIME_FIXED_ONLY 1\n'))

    def test_long_module_list_uses_ccpsx_control_file(self):
        data = self.project.with_name('game_psx_data')
        data.mkdir()
        for i in range(20):
            name = f'logic{i}.c'
            (data / name).write_text('#include "object.h"\nconst PsxLogic psx_logic={0,0,0};\n')
            obj = dict(self.data['objects'][0], id=i+1, module=name)
            if i == 0:
                self.data['objects'] = [obj]
            else:
                self.data['objects'].append(obj)
        self.save()
        self.prepare()
        output = self.output()
        command = next(line.strip() for line in (output / 'src/makefile.mak').read_text().splitlines() if 'ccpsx ' in line)
        self.assertIn('@sources.rsp', command)
        self.assertLess(len(command.replace('$(CFLAGS)', '-O3 -Xo$80010000 -Wall -pedantic-errors')), 126)
        sources = (output / 'src/sources.rsp').read_text().split()
        self.assertEqual(len([s for s in sources if s.startswith('M')]), 20)
        self.assertIn('card.c', sources)

    def test_failed_export_removes_stale_success_artifacts(self):
        self.prepare()
        output = self.output()
        stale = ('src/MAIN.EXE', 'src/MAIN.CPE', 'src/BUILD.LOG', 'cdrom/GAME.ISO', 'cdrom/game.cue', 'SUCCESS.OK', 'build-info.json')
        for name in stale:
            (output / name).write_text('old artifact')
        latest = self.root / 'build/last-ps1-build.txt'
        latest.write_text(str(output) + '\n')
        self.data['objects'][0]['asset'] = 'not-yet-supported.obj'
        self.save()
        with self.assertRaises((RuntimeError, OSError)):
            self.prepare()
        for name in stale:
            self.assertFalse((output / name).exists(), name)
        self.assertFalse(latest.exists())

    def test_configured_output_directory(self):
        self.data['output_path'] = 'exports'
        self.save()
        self.prepare()
        outputs = list((self.root / 'exports').iterdir())
        self.assertEqual(len(outputs), 1)
        self.assertEqual((outputs[0] / '.psx-studio-build').read_text(), str(self.project))
        self.assertTrue((outputs[0] / 'src/scene.c').is_file())
        self.assertFalse((self.root / 'build/ps1').exists())

    def test_same_filename_cannot_overwrite_another_project(self):
        self.prepare()
        output = self.output()
        sentinel = output / 'SUCCESS.OK'
        sentinel.write_text('previous success')
        second = self.root / 'another' / 'game.psx'
        second.parent.mkdir()
        second.write_text(json.dumps(self.data))
        self.project = second
        with self.assertRaisesRegex(RuntimeError, 'another project'):
            self.prepare()
        self.assertEqual(sentinel.read_text(), 'previous success')

    def test_referenced_vag_is_snapshotted_and_embedded(self):
        data = self.project.with_name('game_psx_data')
        data.mkdir()
        shutil.copy2(ROOT / 'examples/shadow-ops_psx_data/shot.vag', data / 'shot.vag')
        (data / 'logic.c').write_text('/* @sound shot.vag */\n#include "object.h"\nconst PsxLogic psx_logic={0,0,0};\n')
        self.data['objects'][0]['module'] = 'logic.c'
        self.save()
        self.prepare()
        output = self.output()
        self.assertEqual((output / 'data/shot.vag').read_bytes(), (data / 'shot.vag').read_bytes())
        self.assertIn('sound_bank_1', (output / 'src/S000.C').read_text())
        # A removed source must not be satisfied by the last build's old copy.
        (data / 'shot.vag').unlink()
        with self.assertRaises(RuntimeError):
            self.prepare()
        self.assertFalse((output / 'data/shot.vag').exists())

    def test_native_texture_preparation(self):
        self.data = json.loads((ROOT / 'examples/first-scene.psx').read_text())
        shutil.copytree(ROOT / 'examples/first-scene_psx_data',
                        self.project.with_name('game_psx_data'))
        self.save()
        self.prepare()
        output = self.output()
        manifest = json.loads((output / 'src/assets.json').read_text())
        self.assertEqual(len(manifest['textures']), 6)
        for texture in manifest['textures']:
            tim = (output / 'data' / texture['file']).read_bytes()
            size = 20 + 2 * texture['width'] * texture['height']
            self.assertEqual(len(tim), size)
            self.assertEqual(struct.unpack_from('<III4H', tim),
                             (16, 2, size - 8, texture['x'], texture['y'],
                              texture['width'], texture['height']))
            self.assertEqual(tim, (output / 'src' / texture['file']).read_bytes())
        self.assertIn('Native TIM:', (output / 'texture.log').read_text())

    def test_all_scene_resources_are_prepared_and_stale_scene_files_removed(self):
        self.data = json.loads((ROOT / 'examples/first-scene.psx').read_text())
        shutil.copytree(ROOT / 'examples/first-scene_psx_data', self.project.with_name('game_psx_data'))
        self.save()
        self.prepare()
        output = self.output()
        manifest = json.loads((output / 'src/assets.json').read_text())
        self.assertEqual(len(manifest['textures']), 6)
        self.assertEqual(len(manifest['modules']), 2)
        self.assertTrue((output / 'src/S001.C').is_file())
        self.assertTrue((output / 'data/player_bounds.c').is_file())
        for texture in manifest['textures']:
            self.assertTrue((output / 'data' / texture['file']).is_file())
            self.assertIn('File ' + texture['file'], (output / 'cdrom/CONF.CTI').read_text())
        # Keep the output-management fixture independent of demo assets/Scenes.
        self.data = {
            'version': 1, 'name': 'Pipeline fixture', 'psyq_path': '',
            'camera': [0, 0, -600],
            'objects': [{'id': 1, 'name': 'Cube', 'kind': 'mesh',
                         'position': [0, 24, 0], 'rotation': [0, 0, 0],
                         'scale': [1, 1, 1], 'bounds': [24, 24, 24],
                         'solid': True, 'asset': '', 'module': ''}],
        }
        self.save()
        self.prepare()
        self.assertFalse((output / 'src/S001.C').exists())

    def test_concurrent_build_does_not_invalidate_running_output(self):
        self.prepare()
        output = self.output()
        exe = output / 'src/MAIN.EXE'
        exe.write_text('in progress')
        with build_lock(output / '.build.lock'):
            with self.assertRaisesRegex(RuntimeError, 'already running'):
                self.prepare()
            self.assertEqual(exe.read_text(), 'in progress')


class VerifyTests(unittest.TestCase):
    """Synthetic discs exercise native verification without compiling any game."""
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='native-disc-')
        self.root = Path(self.temp.name)
        for name in ('src', 'cdrom', 'data'):
            (self.root / name).mkdir()
        (self.root / 'SUCCESS.OK').touch()
        (self.root / 'src/BUILD.LOG').write_text('Build completed: 0 errors\n')
        (self.root / 'src/MAIN.EXE').write_bytes(b'PS-X EXE' + bytes(56))
        (self.root / 'cdrom/SYSTEM.CNF').write_bytes(b'BOOT = cdrom:\\MAIN.EXE;1\r\n')
        (self.root / 'data/T000.TIM').write_bytes(bytes(range(22)))
        (self.root / 'src/assets.json').write_text(json.dumps({'textures': [{'file': 'T000.TIM'}], 'modules': []}))

    def tearDown(self):
        self.temp.cleanup()

    @staticmethod
    def record(name, sector, count, flags=0):
        name = name.encode('ascii')
        record = bytearray(33 + len(name) + ((33 + len(name)) % 2))
        record[0] = len(record)
        record[2:6] = struct.pack('<I', sector)
        record[6:10] = struct.pack('>I', sector)
        record[10:14] = struct.pack('<I', count)
        record[14:18] = struct.pack('>I', count)
        record[25] = flags
        record[28:32] = b'\x01\x00\x00\x01'
        record[32] = len(name)
        record[33:33+len(name)] = name
        return record

    def image(self, stride):
        offset = 24 if stride == 2352 else 0
        image = bytearray(25 * stride)
        descriptor = bytearray(2048)
        descriptor[:7] = b'\x01CD001\x01'
        descriptor[156:190] = self.record('\x00', 20, 2048, 2)
        root = bytearray()
        for name, sector, host in (('MAIN.EXE;1', 22, 'src/MAIN.EXE'), ('SYSTEM.CNF;1', 23, 'cdrom/SYSTEM.CNF')):
            content = (self.root / host).read_bytes()
            image[sector*stride+offset:sector*stride+offset+len(content)] = content
            root.extend(self.record(name, sector, len(content)))
        root.extend(self.record('ASSETS', 21, 2048, 2))
        texture = (self.root / 'data/T000.TIM').read_bytes()
        entries = self.record('T000.TIM;1', 24, len(texture))
        for sector, content in ((16, descriptor), (20, root), (21, entries), (24, texture)):
            image[sector*stride+offset:sector*stride+offset+len(content)] = content
        (self.root / 'cdrom/GAME.ISO').write_bytes(image)
        return image, offset

    def verify(self):
        return subprocess.run([str(EDITOR), '--build-ps1', '--verify', str(self.root)],
                              capture_output=True, text=True, encoding='utf-8')

    def test_raw_and_cooked_images_and_content_mismatch(self):
        for stride, mode in ((2352, 'MODE2/2352'), (2048, 'MODE1/2048')):
            image, offset = self.image(stride)
            result = self.verify()
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn(mode, (self.root / 'cdrom/game.cue').read_text())
            image[22*stride+offset+10] ^= 1
            (self.root / 'cdrom/GAME.ISO').write_bytes(image)
            result = self.verify()
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('differs', result.stderr)

    def test_invalid_directory_is_rejected(self):
        image, offset = self.image(2352)
        image[20*2352+offset+32] = 255
        (self.root / 'cdrom/GAME.ISO').write_bytes(image)
        result = self.verify()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('Invalid ISO filename', result.stderr)


if __name__ == '__main__':
    unittest.main()
