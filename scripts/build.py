#!/usr/bin/env python3
"""Build and validate the Phase 1 download on the GitHub Linux x64 runner."""
import datetime
import hashlib
import io
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tarfile
import tempfile
from urllib.request import urlopen
import zipfile

ROOT = Path(__file__).resolve().parents[1]


def run(*args, capture=False):
    print('+', ' '.join(map(str, args)), flush=True)
    return subprocess.run(args, cwd=ROOT, check=True, text=True,
                          stdout=subprocess.PIPE if capture else None).stdout


def require(condition, message):
    if not condition:
        raise ValueError(message)


def validate_image(esptool, path):
    info = run(esptool, '--chip', 'esp32c3', 'image-info', path, capture=True)
    print(info, flush=True)
    image = path.read_bytes()
    require(len(image) >= 24 and image[0] == 0xE9
            and int.from_bytes(image[12:14], 'little') == 5,
            f'{path.name}: not an ESP32-C3 image')
    require(re.search(r'Checksum: .* \(valid\)', info)
            and re.search(r'Validation hash: .* \(valid\)', info),
            f'{path.name}: bad or missing checksum/validation hash')
    return image


def html_header(html):
    require(html and b'\0' not in html, 'HTML is empty or contains NUL bytes')
    return ('#pragma once\nstatic const unsigned char LUMINA_HTML[] = {\n' +
            ''.join('  ' + ','.join(str(b) for b in html[i:i+24]) + ',\n'
                    for i in range(0, len(html), 24)) + '  0\n};\n')


def build(work):
    cfg = json.loads((ROOT / 'config/build-config.json').read_text())
    version = cfg['version']
    commit = run('git', 'rev-parse', 'HEAD', capture=True).strip()
    number = os.environ['GITHUB_RUN_NUMBER']
    date = datetime.datetime.now(datetime.timezone.utc).strftime('%Y-%m-%d')
    require(re.fullmatch(r'\d+\.\d+\.\d+', version) and number.isdecimal(),
            'Invalid version or CI build number')
    if os.environ['GITHUB_EVENT_NAME'] == 'release':
        require(os.environ['GITHUB_REF_NAME'] == f'v{version}',
                f'Release tag must be v{version}; update config before tagging')

    cli = work / 'arduino-cli'
    cli_version = cfg['arduino_cli']
    archive = f'arduino-cli_{cli_version}_Linux_64bit.tar.gz'
    url = f'https://github.com/arduino/arduino-cli/releases/download/v{cli_version}/{archive}'
    print(f'Downloading {url}', flush=True)
    with urlopen(url, timeout=120) as response:
        payload = response.read()
    require(hashlib.sha256(payload).hexdigest() == cfg['arduino_cli_linux_x64_sha256'],
            'Arduino CLI download SHA-256 mismatch')
    with tarfile.open(fileobj=io.BytesIO(payload), mode='r:gz') as tar:
        cli.write_bytes(tar.extractfile('arduino-cli').read())
    cli.chmod(0o755)
    os.environ['ARDUINO_DIRECTORIES_DATA'] = str(work / 'data')
    os.environ['ARDUINO_DIRECTORIES_DOWNLOADS'] = str(work / 'downloads')
    os.environ['ARDUINO_DIRECTORIES_USER'] = str(work / 'user')
    index = ('--additional-urls', cfg['board_index'])
    run(cli, 'version')
    run(cli, 'core', 'update-index', *index)
    run(cli, 'core', 'install', cfg['core'], *index)
    run(cli, 'core', 'list')
    for lib in cfg['libraries']:
        require('@' in lib and lib.rsplit('@', 1)[1], f'Library must be version-pinned: {lib}')
        run(cli, 'lib', 'install', '--no-deps', lib)
    run(cli, 'board', 'details', '--fqbn', cfg['fqbn'])

    sketch = work / 'LUMINA_STUDIO'
    sketch.mkdir()
    # Compile the ordered sketch as C++ to avoid Arduino's generated prototypes
    # crossing the private namespace or preceding custom types.
    (sketch / 'LUMINA_STUDIO.ino').write_text('// See LUMINA_STUDIO.cpp\n')
    shutil.copyfile(ROOT / 'LUMINA_STUDIO.ino', sketch / 'LUMINA_STUDIO.cpp')
    for header in ROOT.glob('lumina_*.h'):
        shutil.copyfile(header, sketch / header.name)
    run('python3', 'scripts/bundle.py', '--check')
    run('node', 'scripts/test_color.js')
    run('python3', 'scripts/test_effects.py')
    html = (ROOT / 'LUMINA_STUDIO.html').read_bytes()
    (sketch / 'lumina_html.h').write_text(html_header(html), encoding='ascii')
    subprocess.run(['python3', str(ROOT / 'scripts/test_parser.py')], cwd=ROOT, check=True)
    metadata = {'VERSION': version, 'BUILD': number, 'COMMIT': commit, 'DATE': date}
    for key in ('led_gpio', 'led_count', 'led_color_order', 'led_max_brightness'):
        value = cfg[key]
        require(type(value) is (str if key == 'led_color_order' else int), f'Invalid {key} type')
        metadata[key.upper()] = value
    (sketch / 'lumina_build_info.h').write_text(''.join(
        f'#define LUMINA_{key} {json.dumps(value)}\n' for key, value in metadata.items()))
    output = work / 'compiled'
    run(cli, 'compile', '--fqbn', cfg['fqbn'], '--build-path', output,
        '--warnings', 'default', sketch)

    esptool = work / 'data/packages/esp32/tools/esptool_py/5.0.0/esptool'
    prefix = 'LUMINA_STUDIO.ino'
    app = validate_image(esptool, output / f'{prefix}.bin')
    boot = validate_image(esptool, output / f'{prefix}.bootloader.bin')
    partitions = (output / f'{prefix}.partitions.bin').read_bytes()
    boot_app = (work / 'data/packages/esp32/hardware/esp32' /
                cfg['core'].split('@')[1] / 'tools/partitions/boot_app0.bin').read_bytes()
    require(boot[2:4] == b'\x02\x20', 'Bootloader does not declare DIO / 40 MHz / 4 MB')
    require(partitions.startswith(b'\xaa\x50'), 'Invalid partition table magic')
    require(len(boot_app) == 0x2000, 'Unexpected OTA initializer size')
    require(len(app) <= 0x140000, 'Application exceeds the default OTA slot')
    merged = (output / f'{prefix}.merged.bin').read_bytes()
    require(len(merged) == 4 * 1024 * 1024, 'Merged image is not padded to 4 MiB')
    components = [(0, boot), (0x8000, partitions), (0xe000, boot_app), (0x10000, app)]
    cursor = 0
    for offset, data in components:
        require(cursor <= offset and merged[cursor:offset] == b'\xff' * (offset - cursor)
                and merged[offset:offset + len(data)] == data,
                f'Merged image layout mismatch at 0x{offset:x}')
        cursor = offset + len(data)
    require(merged[cursor:] == b'\xff' * (len(merged) - cursor), 'Bad merged image padding')

    dist = ROOT / 'dist'
    dist.mkdir(exist_ok=False)
    package = dist / 'download'
    package.mkdir()
    firmware = package / 'LUMINA_STUDIO.bin'
    firmware.write_bytes(merged)
    for name in ('LUMINA_STUDIO.ino', 'LUMINA_STUDIO.html', 'FLASHING.md', 'WIRING.md', 'EFFECTS.md'):
        require((ROOT / name).stat().st_size > 0, f'Empty source file: {name}')
        shutil.copyfile(ROOT / name, package / name)
    for header in ROOT.glob('lumina_*.h'):
        shutil.copyfile(header, package / header.name)
    manifest = dict(version=version, build=number, commit=commit, build_date_utc=date,
                    core=cfg['core'], fqbn=cfg['fqbn'], format='merged raw flash image',
                    flash_offset='0x0', merged_bytes=len(merged), app_bytes=len(app),
                    sha256=hashlib.sha256(merged).hexdigest())
    report = json.dumps(manifest, indent=2) + '\n'
    (package / 'BUILD-INFO.json').write_text(report)
    print(report, flush=True)
    if os.environ['GITHUB_EVENT_NAME'] == 'release':
        with zipfile.ZipFile(dist / 'LUMINA_STUDIO.zip', 'w', zipfile.ZIP_DEFLATED) as bundle:
            for file in sorted(package.iterdir()):
                bundle.write(file, file.name)
    with open(os.environ['GITHUB_STEP_SUMMARY'], 'a') as summary:
        summary.write(f'### Validated firmware\n\n'
                      f'- Merged: **{len(merged):,} bytes**, flash at `0x0`\n'
                      f'- Application: **{len(app):,} bytes**\n'
                      '- Standalone HTML and sketch packaged\n')


if __name__ == '__main__':
    try:
        with tempfile.TemporaryDirectory(prefix='lumina-') as directory:
            build(Path(directory))
    except (OSError, KeyError, ValueError, subprocess.CalledProcessError) as exc:
        raise SystemExit(f'LUMINA build failed: {exc}') from exc
