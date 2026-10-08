"""Assemble a 16 MiB QSPI release from built images; never access hardware."""
import argparse
import hashlib
import json
import shutil
import subprocess
import tempfile
from pathlib import Path
from qspi_layout import ALIGN, FLASH_BYTES, SETTINGS_BYTES, align, image_layout


def tool(directory, name, *arguments):
    return subprocess.check_output([str(directory / name), *map(str, arguments)], text=True)


def property_u64(tools, dtb, name):
    words = tool(tools, 'fdtget', '-t', 'x', dtb, '/options/u-boot', name).split()
    if len(words) != 2:
        raise ValueError(f'{name}: expected a 64-bit Device Tree property')
    return (int(words[0], 16) << 32) | int(words[1], 16)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('images', type=Path, help='Linux release containing BOOT.BIN and rootfs')
    parser.add_argument('--host-tools', type=Path, required=True, help='Buildroot host/bin')
    args = parser.parse_args()
    images, tools = args.images.resolve(), args.host_tools.resolve()
    files = [images / name for name in ('BOOT.BIN', 'zImage', 'oscill-zynq7020.dtb', 'rootfs.squashfs')]
    script_offset = property_u64(tools, files[2], 'bootscr-flash-offset')
    script_size = property_u64(tools, files[2], 'bootscr-flash-size')
    if script_offset % ALIGN or script_size % ALIGN or script_offset + script_size != FLASH_BYTES:
        raise ValueError('Boot script must occupy the final aligned QSPI region')
    with tempfile.TemporaryDirectory() as directory:
        work = Path(directory)
        patched = work / 'oscill-zynq7020-qspi.dtb'
        source_dtb = files[2]
        for attempt in range(2):
            layout, end = image_layout(zip(('boot', 'kernel', 'dtb', 'rootfs'), files))
            settings_offset = end + 2 * ALIGN
            if settings_offset + SETTINGS_BYTES > script_offset:
                raise ValueError(f'QSPI capacity exceeded by {settings_offset + SETTINGS_BYTES - script_offset} bytes')
            partitions = [(p['label'], p['offset'], p['allocated'], True) for p in layout]
            partitions += [('environment-reserve', end, 2 * ALIGN, True),
                           ('settings', settings_offset, SETTINGS_BYTES, False),
                           ('boot-script', script_offset, script_size, True)]
            nodes = '\n'.join(f'partition@{offset:x} {{ label = "{label}"; reg = <0x{offset:x} 0x{size:x}>; '
                              + ('read-only;' if readonly else '') + ' };'
                              # libfdt prepends new overlay children: emit in reverse address order.
                              for label, offset, size, readonly in reversed(partitions))
            overlay = '/dts-v1/; /plugin/; / { fragment@0 { target-path = "/axi/spi@e000d000/flash@0"; '
            overlay += '__overlay__ { partitions { compatible = "fixed-partitions"; #address-cells = <1>; #size-cells = <1>; '
            overlay += nodes + ' }; }; }; };\n'
            (work / 'qspi-partitions.dtso').write_text(overlay)
            tool(tools, 'dtc', '-@', '-I', 'dts', '-O', 'dtb', '-o', work / 'partitions.dtbo', work / 'qspi-partitions.dtso')
            tool(tools, 'fdtoverlay', '-i', source_dtb, '-o', patched, work / 'partitions.dtbo')
            if align(patched.stat().st_size) == layout[2]['allocated']:
                files[2] = patched
                layout[2]['bytes'] = patched.stat().st_size
                break
            files[2] = patched
        else:
            raise ValueError('Device Tree allocation did not stabilize')
        kernel, dtb = layout[1], layout[2]
        command = 'setenv bootargs console=ttyPS0,115200 root=/dev/mtdblock3 rootfstype=squashfs ro rootwait\n'
        command += 'if sf probe 0 25000000 0; then\n'
        command += f"  if sf read 0x03000000 0x{kernel['offset']:x} 0x{kernel['allocated']:x}; then\n"
        command += f"    if sf read 0x01800000 0x{dtb['offset']:x} 0x{dtb['allocated']:x}; then\n"
        command += '      bootz 0x03000000 - 0x01800000\n    fi\n  fi\nfi\necho Oscill QSPI boot failed\n'
        (work / 'qspi-boot.cmd').write_text(command)
        tool(tools, 'mkimage', '-A', 'arm', '-O', 'linux', '-T', 'script', '-C', 'none',
             '-a', '0', '-e', '0', '-n', 'Oscill QSPI boot', '-d', work / 'qspi-boot.cmd', work / 'qspi.scr')
        script = (work / 'qspi.scr').read_bytes()
        if len(script) > script_size:
            raise ValueError('Boot script exceeds its QSPI allocation')
        image = bytearray(b'\xff') * FLASH_BYTES
        for part, path in zip(layout, files):
            data = path.read_bytes()
            if len(data) != part['bytes'] or len(data) > part['allocated']:
                raise ValueError(f'{path} changed during packaging')
            image[part['offset']:part['offset'] + len(data)] = data
            part['sha256'] = hashlib.sha256(data).hexdigest()
        image[script_offset:script_offset + len(script)] = script
        manifest = dict(flash_bytes=FLASH_BYTES, script_offset=script_offset, script_allocated=script_size,
                        script_bytes=len(script), script_sha256=hashlib.sha256(script).hexdigest(),
                        settings_offset=settings_offset, settings_bytes=SETTINGS_BYTES,
                        free_bytes=script_offset - settings_offset - SETTINGS_BYTES,
                        partitions=layout, hardware_tested=False)
        manifest['image_sha256'] = hashlib.sha256(image).hexdigest()
        for name in ('oscill-zynq7020-qspi.dtb', 'qspi.scr', 'qspi-boot.cmd', 'qspi-partitions.dtso'):
            shutil.copyfile(work / name, images / name)
        (images / 'qspi-layout.json').write_text(json.dumps(manifest, indent=2) + '\n')
        (images / 'qspi.bin').write_bytes(image)
        print(f"QSPI image: {images / 'qspi.bin'}, free {manifest['free_bytes']} bytes plus settings/reserves")


if __name__ == '__main__':
    main()
