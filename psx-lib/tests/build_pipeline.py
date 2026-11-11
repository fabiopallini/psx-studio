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
EDITOR = Path(os.environ.get('PSX_BUILD_TOOL', str(ROOT / 'build' / ('psx-lib.exe' if os.name == 'nt' else 'psx-lib'))))


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
        self.source = self.root / 'game'
        self.source.mkdir()
        (self.source / 'main.c').write_text('#include "object.h"\nint main(void){return 0;}\n')

    def prepare(self, *extra):
        result = self.prepare_c_sources(self.source, *extra)
        if result.returncode:
            raise RuntimeError(result.stdout + result.stderr)

    def tearDown(self):
        self.temp.cleanup()


    def output(self):
        paths = list((self.root / 'build/ps1').iterdir())
        self.assertEqual(len(paths), 1)
        return paths[0]


    @staticmethod
    def compile_commands(output):
        return [line.strip() for line in (output / 'src/makefile.mak').read_text().splitlines()
                if line.strip().startswith('ccpsx ') and ' -c ' in line]


    @staticmethod
    def publish_test_receipts(output):
        # Simulated object bytes in the fixture only: no compiler or playable CD.
        import hashlib
        plan = json.loads((output / 'obj/pending.json').read_text())
        for source, unit in plan['units'].items():
            data = ('fixture-object:' + unit['fingerprint']).encode('ascii')
            (output / 'obj' / (Path(source).stem.upper() + '.OBJ')).write_bytes(data)
            unit['object_hash'] = hashlib.sha256(data).hexdigest()
        (output / 'obj/cache.json').write_text(json.dumps(plan))


    def test_incremental_objects_reused_without_timestamp_dependencies(self):
        self.prepare()
        output = self.output()
        first = self.compile_commands(output)
        self.assertGreater(len(first), 0)
        self.publish_test_receipts(output)
        before = (output / 'obj/MAIN.OBJ').stat().st_mtime_ns
        self.prepare()
        self.assertEqual(self.compile_commands(output), [])
        self.assertEqual((output / 'obj/MAIN.OBJ').stat().st_mtime_ns, before)
        self.assertIn('@objects.rsp', (output / 'src/makefile.mak').read_text())
        self.assertIn('..\\obj\\MAIN.OBJ', (output / 'src/objects.rsp').read_text())


    def test_incremental_runtime_header_dependency_and_sdk_change(self):
        self.prepare()
        output = self.output()
        self.publish_test_receipts(output)
        header = self.root / 'runtime/object.h'
        # Same-size replacement with original mtime defeats a timestamp-only cache.
        stamp = header.stat()
        data = header.read_bytes().replace(b'Portable', b'portable', 1)
        self.assertEqual(len(data), stamp.st_size)
        header.write_bytes(data)
        os.utime(header, ns=(stamp.st_atime_ns, stamp.st_mtime_ns))
        self.prepare()
        self.assertGreater(len(self.compile_commands(output)), 0)
        self.publish_test_receipts(output)
        compiler = self.sdk / 'psyq/CCPSX.EXE'
        compiler.write_bytes(b'new SDK driver')
        self.prepare()
        plan = json.loads((output / 'obj/pending.json').read_text())
        self.assertEqual(len(self.compile_commands(output)), len(plan['units']))


    def test_incremental_compile_flags_invalidate_cached_objects(self):
        self.prepare()
        output = self.output()
        self.publish_test_receipts(output)
        self.prepare('--diagnostics')
        plan = json.loads((output / 'obj/pending.json').read_text())
        self.assertEqual(len(self.compile_commands(output)), len(plan['units']))
        self.assertIn('-DSTUDIO_DIAGNOSTICS', (output / 'src/makefile.mak').read_text())


    def test_incremental_corrupt_object_and_uncommitted_outputs_rebuild(self):
        self.prepare()
        output = self.output()
        self.publish_test_receipts(output)
        (output / 'obj/MAIN.OBJ').write_bytes(b'damaged object')
        self.prepare()
        self.assertEqual(len(self.compile_commands(output)), 1)
        self.assertIn(' -c main.c ', self.compile_commands(output)[0])
        # A failed/interrupted SDK run cannot publish new receipts.
        (output / 'obj/MAIN.OBJ').write_bytes(b'partial object')
        self.prepare()
        self.assertEqual(len(self.compile_commands(output)), 1)
        self.assertFalse((output / 'obj/MAIN.OBJ').exists())


    def test_incremental_removes_obsolete_objects_and_ignores_invalid_receipts(self):
        self.prepare()
        output = self.output()
        self.publish_test_receipts(output)
        (output / 'obj/M999.OBJ').write_bytes(b'obsolete unit')
        self.prepare()
        self.assertFalse((output / 'obj/M999.OBJ').exists())
        self.assertNotIn('M999', (output / 'src/objects.rsp').read_text())
        (output / 'obj/cache.json').write_text('{bad json')
        self.prepare()
        self.assertGreater(len(self.compile_commands(output)), 0)


    @unittest.skipIf(os.name == 'nt', 'Executable fixture uses a POSIX shebang.')
    def test_compiler_diagnostics_survive_dosbox_failure(self):
        import sys
        fake = self.root / 'dosbox-x'
        environment = dict(os.environ, PATH=str(self.root) + os.pathsep + os.environ.get('PATH', ''))
        compiler = b"PsyMake\r\nlogic.c:12: error: structure has no member named hud\r\nCP437: \x82\r\n"
        for exit_code in (1, 0):
            with self.subTest(dosbox_exit=exit_code):
                fake.write_text('#!' + sys.executable + '\nfrom pathlib import Path\nimport sys\n'
                                + 'Path("src/BUILD.LOG").write_bytes(' + repr(compiler) + ')\n'
                                + 'Path("FAILED.TXT").write_text("failed")\n'
                                + 'print("DOSBox fixture reached the compiler")\n'
                                + f'sys.exit({exit_code})\n')
                fake.chmod(0o755)
                result = subprocess.run([str(EDITOR), '--build-ps1', '--source-root', str(self.source),
                                         '--psyq', str(self.sdk), '--root', str(self.root)],
                                        env=environment, capture_output=True, text=True)
                self.assertNotEqual(result.returncode, 0)
                combined = result.stdout + result.stderr
                self.assertIn('logic.c:12: error: structure has no member named hud', combined)
                self.assertIn('CP437: é', combined)
                if exit_code:
                    self.assertIn('Psy-Q compiler log', combined)
                    self.assertIn('DOSBox fixture reached the compiler', combined)

    def test_preparation_discards_stale_dosbox_diagnostics(self):
        self.prepare()
        log = self.output() / 'dosbox.log'
        log.write_text('OLD BUILD FAILURE')
        self.prepare()
        self.assertFalse(log.exists())

    @unittest.skipIf(os.name == 'nt', 'Executable fixture uses a POSIX shebang.')
    def test_dosbox_x_uses_isolated_config_and_larger_private_area(self):
        import sys
        fake = self.root / 'dosbox-x'
        calls = self.root / 'dosbox-args.json'
        fake.write_text('#!' + sys.executable + '\nimport json,sys\nfrom pathlib import Path\n'
                        'if "--version" in sys.argv:\n'
                        ' assert "-noprimaryconf" in sys.argv and "-nolocalconf" in sys.argv\n'
                        ' print("DOSBox Staging version 0.82.2", file=sys.stderr)\n'
                        'else:\n'
                        f' Path({str(calls)!r}).write_text(json.dumps(sys.argv))\n'
                        ' sys.exit(1)\n')
        fake.chmod(0o755)
        environment = dict(os.environ, PATH=str(self.root) + os.pathsep + os.environ.get('PATH', ''))
        result = subprocess.run([str(EDITOR), '--build-ps1', '--source-root', str(self.source),
                                 '--psyq', str(self.sdk), '--root', str(self.root)],
                                env=environment, capture_output=True, text=True)
        self.assertNotEqual(result.returncode, 0)  # fixture never compiles or creates a CD
        self.assertTrue(calls.exists())
        arguments = json.loads(calls.read_text())
        self.assertEqual(Path(arguments[0]).name, 'dosbox-x')
        self.assertIn('-defaultconf', arguments)
        self.assertIn('-conf', arguments)
        self.assertNotIn('Deprecated option', result.stderr)
        output = self.output()
        config = (output / 'dos.conf').read_text()
        self.assertIn('private area size=131072', config)
        self.assertIn('ems=false', config)
        self.assertIn('cycles=fixed 200000', config)
        makefile = (output / 'src/makefile.mak').read_text()
        self.assertEqual(makefile.count('16bit:'), 1)
        self.assertNotRegex(makefile, r'(?m)^C[0-9]+:')
        self.assertFalse((output / 'obj/cache.json').exists())
        self.assertFalse((output / 'SUCCESS.OK').exists())


    @unittest.skipIf(os.name == 'nt', 'Executable fixture uses a POSIX shebang.')
    def test_missing_dosbox_x_does_not_fall_back(self):
        import sys
        marker = self.root / 'unexpected-launcher'
        for name in ('dosbox', 'flatpak'):
            executable = self.root / name
            executable.write_text('#!' + sys.executable + '\nfrom pathlib import Path\n'
                                  f'Path({str(marker)!r}).touch()\n')
            executable.chmod(0o755)
        environment = dict(os.environ, PATH=str(self.root))
        result = subprocess.run([str(EDITOR), '--build-ps1', '--source-root', str(self.source),
                                 '--psyq', str(self.sdk), '--root', str(self.root)],
                                env=environment, capture_output=True, text=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('DOSBox-X was not found', result.stderr)
        self.assertFalse(marker.exists())


    def prepare_c_sources(self, source, *extra):
        result = subprocess.run([str(EDITOR), '--build-ps1', '--source-root', str(source),
                                 '--psyq', str(self.sdk), '--root', str(self.root), '--prepare-only',
                                 '--max-objects', '4', '--render-triangles', '256', *extra],
                                capture_output=True, text=True)
        return result


    def test_c_sources_with_converted_resource_banks(self):
        source = self.root / 'plain-c'
        source.mkdir()
        (source / 'main.c').write_text('#include "assets.h"\nint main(void) {return psx_resources(7)==0;}\n')
        model = self.root / 'triangle.obj'
        model.write_text('v 0 0 0\nv 10 0 0\nv 0 10 0\nf 1 2 3\n')
        resources = source / 'reslist.h'
        resources.write_text('PSX_BANK(7, "")\nPSX_MODEL(2, mesh, "triangle.obj", "")\nPSX_END()\n')
        result = subprocess.run([str(EDITOR), '--build-ps1', '--source-root', str(source),
                                 '--resources', str(resources), '--asset-root', str(self.root),
                                 '--psyq', str(self.sdk), '--root', str(self.root), '--prepare-only',
                                 '--name', 'custom-game'], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        output = self.output()
        self.assertEqual(output.name, 'custom-game')
        self.assertFalse((output / 'project.snapshot.psx').exists())
        control = (output / 'src/sources.rsp').read_text()
        self.assertIn('rb007.c', control)
        self.assertIn('resbank.c', control)
        self.assertTrue((output / 'src/assets.h').exists())
        self.assertNotIn('object_defaults', (output / 'src/rb007.c').read_text())
        self.publish_test_receipts(output)
        model.write_text('v 0 0 0\nv 12 0 0\nv 0 10 0\nf 1 2 3\n')
        result = subprocess.run([str(EDITOR), '--build-ps1', '--source-root', str(source),
                                 '--resources', str(resources), '--asset-root', str(self.root),
                                 '--psyq', str(self.sdk), '--root', str(self.root), '--prepare-only',
                                 '--name', 'custom-game'], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertTrue(any('rg000.c' in command for command in self.compile_commands(output)))


    def test_c_build_definition_invalidates_only_including_units(self):
        source = self.root / 'plain-c'
        source.mkdir()
        (source / 'main.c').write_text('#include "psxdefs.h"\n#ifndef START\n#define START 1\n#endif\nint main(void) {return START;}\n')
        (source / 'other.c').write_text('int other_value=0;\n')
        result = self.prepare_c_sources(source, '--define', 'START=5')
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        output = self.output()
        self.assertIn('#define START 5', (output / 'src/psxdefs.h').read_text())
        self.publish_test_receipts(output)
        result = self.prepare_c_sources(source, '--define', 'START=6')
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        commands = self.compile_commands(output)
        self.assertEqual(len(commands), 1)
        self.assertIn(' -c main.c ', commands[0])
        result = self.prepare_c_sources(source)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertNotIn('#define START', (output / 'src/psxdefs.h').read_text())
        result = self.prepare_c_sources(source, '--define', 'START=5;bad')
        self.assertNotEqual(result.returncode, 0)


    def test_c_source_project_needs_no_scene_document(self):
        source = self.root / 'plain-c'
        source.mkdir()
        (source / 'main.c').write_text('#include "data.h"\nint main(void) {return VALUE;}\n')
        (source / 'data.h').write_text('#define VALUE 0\n')
        result = self.prepare_c_sources(source)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        output = self.output()
        self.assertEqual((output / 'src/main.c').read_text().replace('\r\n', '\n'), (source / 'main.c').read_text())
        self.assertFalse((output / 'project.snapshot.psx').exists())
        self.assertFalse((output / 'src/scene.c').exists())
        self.assertIn('object.c', (output / 'src/sources.rsp').read_text())
        self.assertNotIn('scene.c', (output / 'src/sources.rsp').read_text())
        self.publish_test_receipts(output)
        (source / 'data.h').write_text('#define VALUE 1\n')
        result = self.prepare_c_sources(source)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertTrue(any(' -c main.c ' in command for command in self.compile_commands(output)))


    def test_c_source_project_packages_tim_and_additional_c_unit(self):
        source = self.root / 'plain-c'
        source.mkdir()
        (source / 'main.c').write_text('int main(void) {return 0;}\n')
        (source / 'custom.c').write_text('int custom_value=1;\n')
        image = struct.pack('<IIIHHHH', 16, 2, 12+32*32*2, 320, 256, 32, 32) + bytes(32*32*2)
        (source / 'image.tim').write_bytes(image)
        result = self.prepare_c_sources(source)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        output = self.output()
        self.assertIn('custom.c', (output / 'src/sources.rsp').read_text())
        self.assertEqual((output / 'data/IMAGE.TIM').read_bytes(), image)
        self.assertIn('File IMAGE.TIM', (output / 'cdrom/CONF.CTI').read_text())


    def test_c_source_project_cannot_replace_runtime(self):
        source = self.root / 'plain-c'
        source.mkdir()
        (source / 'main.c').write_text('int main(void) {return 0;}\n')
        (source / 'psx.c').write_text('int replacement;\n')
        result = self.prepare_c_sources(source)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('replace an engine runtime file', result.stderr)


    def test_concurrent_build_does_not_invalidate_running_output(self):
        self.prepare()
        output = self.output()
        exe = output / 'src/MAIN.EXE'
        exe.write_text('in progress')
        with build_lock(output / '.build.lock'):
            with self.assertRaisesRegex(RuntimeError, 'already running'):
                self.prepare()
            self.assertEqual(exe.read_text(), 'in progress')


    def test_document_cli_options_are_rejected(self):
        document = self.root / 'legacy.psx'
        document.write_text('{"format":"psx-game-1","scenes":[]}')
        for flags in (['--validate', str(document)], ['--export', str(document), '--output', str(self.root / 'export')],
                      ['--build-ps1', '--project', str(document)], ['--build-ps1', '--exporter', 'anything']):
            result = subprocess.run([str(EDITOR), *flags], capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('Unknown option', result.stderr)
        self.assertFalse((self.root / 'export').exists())

    def test_c_source_directory_is_required(self):
        result = subprocess.run([str(EDITOR), '--build-ps1', '--psyq', str(self.sdk), '--root', str(self.root), '--prepare-only'],
                                capture_output=True, text=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('--source-root is required', result.stderr)
        self.assertFalse((self.root / 'build').exists())

    def test_application_edit_only_recompiles_its_unit(self):
        custom = self.source / 'custom.c'
        custom.write_text('int value=1;\n')
        self.prepare()
        output = self.output()
        self.publish_test_receipts(output)
        custom.write_text('int value=2;\n')
        self.prepare()
        commands = self.compile_commands(output)
        self.assertEqual(len(commands), 1)
        self.assertIn(' -c custom.c ', commands[0])
        custom.unlink()
        self.prepare()
        self.assertNotIn('custom.c', (output / 'src/sources.rsp').read_text())
        self.assertFalse((output / 'src/custom.c').exists())

    def test_capacity_flags_specialize_buffers(self):
        self.prepare('--max-objects', '37', '--render-triangles', '2048')
        header = (self.output() / 'src/psx.h').read_text()
        self.assertIn('#define STUDIO_OBJECT_LIMIT 37', header)
        self.assertIn('#define STUDIO_POLYGON_LIMIT 2048', header)
        with self.assertRaisesRegex(RuntimeError, 'capacities'):
            self.prepare('--max-objects', '0')

    def test_failed_source_preparation_invalidates_old_artifacts(self):
        self.prepare()
        output = self.output()
        stale = ('src/MAIN.EXE', 'src/MAIN.CPE', 'src/BUILD.LOG', 'cdrom/GAME.ISO', 'cdrom/game.cue', 'SUCCESS.OK', 'build-info.json')
        for name in stale:
            (output / name).write_text('old')
        (self.source / 'psx.c').write_text('int replacement;\n')
        with self.assertRaisesRegex(RuntimeError, 'replace an engine runtime'):
            self.prepare()
        for name in stale:
            self.assertFalse((output / name).exists())

    def test_source_ownership_and_output_override(self):
        self.prepare()
        output = self.output()
        (output / 'SUCCESS.OK').write_text('previous success')
        other = self.root / 'other/game'
        other.mkdir(parents=True)
        (other / 'main.c').write_text('int main(void){return 0;}\n')
        result = self.prepare_c_sources(other)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('another project', result.stderr)
        self.assertEqual((output / 'SUCCESS.OK').read_text(), 'previous success')
        result = self.prepare_c_sources(other, '--output-root', str(self.root / 'exports'))
        self.assertEqual(result.returncode, 0, result.stderr)
        exports = list((self.root / 'exports').iterdir())
        self.assertEqual(len(exports), 1)
        self.assertEqual((exports[0] / '.psx-studio-build').read_text(), str(other))

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
        image = bytearray(26 * stride)
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
        manifest = json.loads((self.root / 'src/assets.json').read_text())
        directory = manifest['textures'][0].get('directory')
        if directory:
            image[25*stride+offset:25*stride+offset+len(entries)] = entries
            entries = self.record(directory, 25, 2048, 2)
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

    def test_c_resource_bank_texture_directories(self):
        for directory in ('B001', 'B022', 'B65535'):
            with self.subTest(directory=directory):
                manifest = {'textures': [{'file': 'T000.TIM', 'directory': directory}], 'modules': []}
                (self.root / 'src/assets.json').write_text(json.dumps(manifest))
                self.image(2352)
                result = self.verify()
                self.assertEqual(result.returncode, 0, result.stderr)
        for directory in ('../B001', 'B001/child', 'B001\\child', 'B12', 'B123456', 'S005'):
            with self.subTest(invalid=directory):
                manifest['textures'][0]['directory'] = directory
                (self.root / 'src/assets.json').write_text(json.dumps(manifest))
                result = self.verify()
                self.assertNotEqual(result.returncode, 0)
                self.assertIn('Invalid texture directory', result.stderr)

    def test_invalid_directory_is_rejected(self):
        image, offset = self.image(2352)
        image[20*2352+offset+32] = 255
        (self.root / 'cdrom/GAME.ISO').write_bytes(image)
        result = self.verify()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('Invalid ISO filename', result.stderr)


if __name__ == '__main__':
    unittest.main()
