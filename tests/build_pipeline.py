"""Studio/engine boundary checks; Python is a developer-test dependency only."""
import contextlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
EDITOR = ROOT / 'build' / ('psx-studio.exe' if os.name == 'nt' else 'psx-studio')
ENGINE_ROOT = Path(os.environ.get('PSX_LIB_ROOT', ROOT / 'psx-lib'))
ENGINE = Path(os.environ.get('PSX_LIB', ENGINE_ROOT / 'build' / ('psx-lib.exe' if os.name == 'nt' else 'psx-lib')))


@contextlib.contextmanager
def build_lock(path):
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
        self.temp = tempfile.TemporaryDirectory(prefix='studio-engine-', dir=ROOT / 'build')
        self.root = Path(self.temp.name).resolve()
        self.project = self.root / 'game.psx'
        self.data = {
            'version': 1, 'name': 'Engine integration', 'psyq_path': '',
            'camera': [0, 0, -600],
            'objects': [{'id': 1, 'name': 'Cube', 'kind': 'mesh',
                         'position': [0, 24, 0], 'rotation': [0, 0, 0],
                         'scale': [1, 1, 1], 'bounds': [24, 24, 24],
                         'solid': True, 'asset': '', 'module': ''}],
        }
        self.sdk = self.root / 'sdk'
        for name in ('psyq', 'include', 'lib'):
            (self.sdk / name).mkdir(parents=True)
        for name in ('CCPSX.EXE', 'PSYMAKE.EXE'):
            (self.sdk / 'psyq' / name).touch()
        self.save()

    def tearDown(self):
        self.temp.cleanup()

    def save(self):
        self.project.write_text(json.dumps(self.data))

    def run_build(self, *extra, engine=ENGINE, success=True, prepare=True, env=None):
        args = [str(EDITOR), '--build-ps1', '--root', str(self.root),
                '--project', str(self.project), '--psyq', str(self.sdk)]
        if engine is not None:
            args += ['--engine', str(engine)]
        if prepare:
            args += ['--prepare-only']
        result = subprocess.run(args + list(extra), capture_output=True, text=True, env=env)
        if success:
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        else:
            self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
        return result

    def sources(self):
        return next((self.root / 'build/studio-projects').glob('*/source'))

    def output(self):
        return next((self.root / 'build/engine/ps1').iterdir())

    def require_engine(self):
        if not ENGINE.is_file():
            self.skipTest('Set PSX_LIB to an external engine with the direct-C build interface.')

    def bundle_framework(self, packaged=False):
        bundled = self.root / 'psx-lib'
        bundled.mkdir()
        for directory in ('runtime', 'pipeline'):
            shutil.copytree(ENGINE_ROOT / directory, bundled / directory)
        if packaged:
            binary = self.root / ('psx-lib.exe' if os.name == 'nt' else 'psx-lib')
        else:
            (bundled / 'build').mkdir()
            binary = bundled / 'build' / ENGINE.name
        shutil.copy2(ENGINE, binary)
        return binary

    def test_bundled_framework_is_selected_without_a_setting(self):
        self.require_engine()
        binary = self.bundle_framework()
        self.data['engine_path'] = '/obsolete/psx-engine'
        self.save()
        result = self.run_build(engine=None)
        self.assertIn(str(binary), result.stdout)
        self.assertTrue((self.output() / 'src/main.c').exists())

    @unittest.skipIf(os.name != 'nt', 'Windows package executable layout')
    def test_packaged_framework_is_selected_without_path_lookup(self):
        self.require_engine()
        binary = self.bundle_framework(packaged=True)
        result = self.run_build(engine=None)
        self.assertIn(str(binary), result.stdout)
        self.assertTrue((self.output() / 'src/main.c').exists())

    def test_explicit_missing_framework_does_not_use_the_bundle(self):
        self.require_engine()
        self.bundle_framework()
        result = self.run_build(engine=self.root / 'missing-lib', success=False)
        self.assertIn('PSX Lib not found', result.stderr)
        self.assertFalse((self.root / 'build/studio-projects').exists())

    @unittest.skipIf(os.name == 'nt', 'Executable fixture uses a POSIX shebang.')
    def test_failed_sdk_compiler_log_reaches_studio(self):
        self.require_engine()
        import sys
        fake = self.root / 'dosbox-x'
        fake.write_text('#!' + sys.executable + '\nfrom pathlib import Path\nimport sys\n'
                        'Path("src/BUILD.LOG").write_text("module.c:7: error: unknown HUD field\\n")\n'
                        'print("Compiler fixture failed")\nsys.exit(1)\n')
        fake.chmod(0o755)
        environment = dict(os.environ, PATH=str(self.root) + os.pathsep + os.environ.get('PATH', ''))
        result = self.run_build(prepare=False, success=False, env=environment)
        self.assertIn('module.c:7: error: unknown HUD field', result.stdout + result.stderr)
        self.assertIn('Psy-Q compiler log', result.stdout + result.stderr)

    def test_missing_engine_is_explicit(self):
        result = self.run_build(engine=self.root / 'missing-engine', success=False)
        self.assertIn('PSX Lib not found', result.stderr)
        self.assertFalse((self.root / 'build/studio-projects').exists())

    def test_no_bundled_runtime_or_sdk_tools(self):
        self.assertFalse((ROOT / 'runtime').exists())
        self.assertFalse((ROOT / 'pipeline').exists())

    @unittest.skipIf(os.name == 'nt', 'Relative POSIX executable-path regression')
    def test_relative_launcher_reuses_canonical_engine_source_identity(self):
        self.require_engine()
        (self.root / 'build').mkdir()
        shutil.copy2(EDITOR, self.root / 'build/psx-studio')
        args = ['./build/psx-studio', '--build-ps1', '--project', str(self.project),
                '--engine', str(ENGINE), '--psyq', str(self.sdk), '--prepare-only']
        for _ in range(2):
            result = subprocess.run(args, cwd=self.root, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual((self.output() / '.psx-studio-build').read_text(), str(self.sources()))

    def test_real_engine_prepares_only_c_sources(self):
        self.require_engine()
        self.run_build()
        source, output = self.sources(), self.output()
        self.assertEqual((output / '.psx-studio-build').read_text(), str(source))
        self.assertNotIn('--project', (output / 'BUILD.BAT').read_text())
        units = (output / 'src/sources.rsp').read_text().lower().split()
        self.assertEqual(units.count('scene.c'), 1)
        self.assertNotIn('s000.h', units)
        self.assertIn('"s000.h"', (source / 'scene.c').read_text())
        self.assertIn('psDrawOverlay()', (source / 'main.c').read_text())
        self.assertNotIn('psDrawHud', (source / 'main.c').read_text())
        self.assertEqual((output / 'src/main.c').read_text(), (source / 'main.c').read_text())
        self.assertIn('#define STUDIO_OBJECT_LIMIT 1', (output / 'src/psx.h').read_text())
        config = (output / 'dos.conf').read_text()
        self.assertIn('private area size=131072', config)
        self.assertIn('ems=false', config)
        self.assertFalse((output / 'SUCCESS.OK').exists())

    def test_generated_application_uses_current_engine_c89_api(self):
        self.require_engine()
        if not shutil.which('cc'):
            self.skipTest('C compiler unavailable')
        self.run_build()
        src = self.output() / 'src'
        headers = self.root / 'host-api'
        headers.mkdir()
        # Keep the actual engine API declarations; host syntax checking does
        # not need Psy-Q system/library types used only inside its backends.
        public = (src / 'psx.h').read_text()
        (headers / 'psx.h').write_text('\n'.join(
            line for line in public.splitlines() if not line.startswith('#include <')))
        shutil.copyfile(src / 'main.c', headers / 'main.c')
        result = subprocess.run(['cc', '-std=c89', '-pedantic-errors', '-Wall', '-Wextra',
                                 '-Werror', '-fsyntax-only', '-I' + str(headers), '-I' + str(src),
                                 str(headers / 'main.c'), str(src / 'scene.c')],
                                capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_failed_export_invalidates_previous_success(self):
        self.require_engine()
        self.run_build()
        output = self.output()
        (output / 'SUCCESS.OK').write_text('success')
        (output / '.studio-project').write_text(str(self.project))
        self.data['objects'][0]['module'] = 'missing.c'
        self.save()
        self.run_build(success=False)
        self.assertFalse((output / 'SUCCESS.OK').exists())
        self.assertFalse((output / '.studio-project').exists())

    def test_texture_is_packaged_by_the_external_engine(self):
        self.require_engine()
        import struct
        import zlib
        def chunk(kind, data):
            return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data))
        data = self.project.with_name('game_psx_data')
        data.mkdir()
        png = (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', 2, 2, 8, 6, 0, 0, 0))
               + chunk(b'IDAT', zlib.compress((b'\0' + bytes([255, 0, 0, 255]) * 2) * 2))
               + chunk(b'IEND', b''))
        (data / 'texture.png').write_bytes(png)
        self.data['objects'][0]['texture'] = 'texture.png'
        self.save()
        self.run_build()
        output = self.output()
        texture = json.loads((output / 'src/assets.json').read_text())['textures'][0]
        self.assertEqual(texture['file'], 'T000.TIM')
        self.assertEqual((output / 'data/T000.TIM').read_bytes(), (output / 'src/T000.TIM').read_bytes())
        self.assertEqual(texture['directory'], 'B001')
        self.assertIn('Directory B001', (output / 'cdrom/CONF.CTI').read_text())
        self.assertIn('File T000.TIM', (output / 'cdrom/CONF.CTI').read_text())
        self.assertIn('ASSETS\\\\B001\\\\T000.TIM', (output / 'src/rb001.c').read_text())

    def test_module_compiles_once_and_stale_sources_disappear(self):
        self.require_engine()
        data = self.project.with_name('game_psx_data')
        data.mkdir()
        (data / 'logic.c').write_text('#include "object.h"\nconst PsxLogic psx_logic={0,0,0};\n')
        self.data['objects'][0]['module'] = 'logic.c'
        self.save()
        self.run_build()
        output = self.output()
        units = (output / 'src/sources.rsp').read_text().lower().split()
        self.assertEqual(units.count('m000.c'), 1)
        self.assertNotIn('l000.h', units)
        self.assertNotIn('l000.c', units)
        self.assertIn('"l000.h"', (output / 'src/m000.c').read_text())
        self.data['objects'][0]['module'] = ''
        self.save()
        self.run_build()
        self.assertFalse((output / 'src/m000.c').exists())
        self.assertFalse((self.sources() / 'l000.h').exists())

    def test_many_textures_use_separate_bank_directories(self):
        self.require_engine()
        import copy
        import struct
        import zlib
        def chunk(kind, data):
            return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data))
        data = self.project.with_name('game_psx_data')
        data.mkdir()
        objects = []
        for i in range(6):
            png = (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', 2, 2, 8, 6, 0, 0, 0))
                   + chunk(b'IDAT', zlib.compress((b'\0' + bytes([i * 30, 30, 90, 255]) * 2) * 2))
                   + chunk(b'IEND', b''))
            (data / f'texture{i}.png').write_bytes(png)
            obj = copy.deepcopy(self.data['objects'][0])
            obj.update(id=i + 1, texture=f'texture{i}.png')
            objects.append(obj)
        scene = dict(self.data, version=3, objects=objects)
        self.data.update(version=3, objects=objects, active_scene=70001, startup_scene=70001,
                         scenes=[dict(id=70001 + i, name=f'Room {i}', data=copy.deepcopy(scene))
                                 for i in range(6)])
        self.save()
        self.run_build()
        output = self.output()
        textures = json.loads((output / 'src/assets.json').read_text())['textures']
        self.assertEqual(len(textures), 36)
        for i in range(6):
            directory = f'B{i + 1:03}'
            self.assertEqual(sum(t['directory'] == directory for t in textures), 6)
            self.assertIn('Directory ' + directory, (output / 'cdrom/CONF.CTI').read_text())
        self.assertIn('psx_bank_6', (output / 'src/s005.h').read_text())
        self.assertIn('case 70006U:', (output / 'src/scene.c').read_text())

    def test_resource_banks_preserve_game_state_and_sound_linkage(self):
        self.require_engine()
        data = self.project.with_name('game_psx_data')
        data.mkdir()
        (data / 'logic.c').write_text('/* @sound shot.vag */\n#include "object.h"\nconst PsxLogic psx_logic={0,0,0};\n')
        sample = bytearray(96)
        sample[:4] = b'VAGp'
        sample[7], sample[15], sample[18], sample[19], sample[81] = 32, 48, 43, 17, 1
        (data / 'shot.vag').write_bytes(sample)
        self.data['objects'][0]['module'] = 'logic.c'
        self.save()
        self.run_build()
        src = self.output() / 'src'
        self.assertIn('PSX_SOUND("shot.vag")', (self.sources() / 'reslist.h').read_text())
        self.assertTrue((src / 'ra000.c').exists())
        self.assertNotIn('sound_bank_', (src / 's000.h').read_text())
        harness = self.root / 'resource-check.c'
        harness.write_text('#include "scene.h"\n#include <assert.h>\nint main(void){PsxScene *s; s=studio_scene_load();'
                           'assert(s->count==1 && s->objects[0].solid);'
                           'assert(s->objects[0].position[1]==24 && s->objects[0].logic);'
                           'assert(s->objects[0].mesh && s->objects[0].mesh->count==36);'
                           'assert(s->sound_count==1 && s->sounds[0].size==64);return 0;}\n')
        if not shutil.which('cc'):
            self.skipTest('C compiler unavailable')
        units = [src / name for name in (src / 'sources.rsp').read_text().split()
                 if name not in ('main.c', 'camera.c', 'psx.c', 'card.c')]
        binary = self.root / 'resource-check'
        result = subprocess.run(['cc', '-std=c89', '-pedantic-errors', '-Wall', '-Wextra', '-Werror',
                                 '-I' + str(src), *map(str, units), str(harness), '-o', str(binary)],
                                capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(subprocess.run([str(binary)]).returncode, 0)

    def test_workspace_lock_prevents_export_races(self):
        self.require_engine()
        self.run_build()
        source = self.sources()
        before = (source / 'scene.c').read_bytes()
        with build_lock(source.parent / '.build.lock'):
            result = self.run_build(success=False)
        self.assertIn('already running', result.stderr)
        self.assertEqual((source / 'scene.c').read_bytes(), before)

    def test_legacy_engine_setting_is_ignored_and_output_override_works(self):
        self.require_engine()
        self.data['engine_path'] = '/obsolete/psx-engine'
        self.save()
        custom = self.root / 'custom output'
        self.run_build('--output-root', str(custom))
        source = self.sources()
        outputs = list(custom.iterdir())
        self.assertEqual(len(outputs), 1)
        self.assertEqual((outputs[0] / '.psx-studio-build').read_text(), str(source))

    def test_engine_failure_is_not_a_successful_build(self):
        self.require_engine()
        self.run_build('--engine', str(ENGINE), '--psyq', str(self.root / 'bad-sdk'), success=False)
        self.assertFalse((self.root / 'build/engine/ps1').exists())

    def test_same_filename_projects_are_isolated(self):
        self.require_engine()
        self.run_build()
        (self.root / 'other').mkdir()
        self.project = self.root / 'other/game.psx'
        self.save()
        self.run_build()
        self.assertEqual(len(list((self.root / 'build/engine/ps1').iterdir())), 2)

    def test_standalone_reexport_removes_obsolete_compilable_units(self):
        data = self.project.with_name('game_psx_data')
        data.mkdir()
        (data / 'logic.c').write_text('#include "object.h"\nconst PsxLogic psx_logic={0,0,0};\n')
        self.data['objects'][0]['module'] = 'logic.c'
        self.save()
        export = self.root / 'standalone'
        args = [str(EDITOR), '--export', str(self.project), '--output', str(export)]
        result = subprocess.run(args, capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertTrue((export / 'm000.c').exists())
        self.data['objects'][0]['module'] = ''
        self.save()
        (export / 'notes.txt').write_text('User notes')
        result = subprocess.run(args, capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertFalse((export / 'm000.c').exists())
        self.assertFalse((export / 'l000.h').exists())
        self.assertEqual((export / 'notes.txt').read_text(), 'User notes')

    def test_export_does_not_overwrite_an_unmanaged_source_tree(self):
        export = self.root / 'user-source'
        export.mkdir()
        (export / 'main.c').write_text('User source')
        result = subprocess.run([str(EDITOR), '--export', str(self.project), '--output', str(export)],
                                capture_output=True, text=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual((export / 'main.c').read_text(), 'User source')

    def test_standalone_export_has_application_entry_point(self):
        export = self.root / 'export'
        result = subprocess.run([str(EDITOR), '--export', str(self.project), '--output', str(export)],
                                capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertTrue((export / 'main.c').is_file())
        self.assertTrue((export / 'scene.c').is_file())
        self.assertTrue((export / 's000.h').is_file())
        self.assertFalse((export / 'object.h').exists())
        self.assertFalse((export / 'psx.c').exists())


if __name__ == '__main__':
    unittest.main()
