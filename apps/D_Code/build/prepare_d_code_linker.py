"""将 D_Code 所需的 SDK 可执行输入放入原厂打包器处理的段，并检查 ELF 覆盖。"""
import argparse
from pathlib import Path
import struct

ROOT = Path(__file__).resolve().parents[3]


def check_packed_executable_sections(sections: dict) -> None:
    """核对 ELF 已分配可执行段均在 SDK 打包集合内。"""
    packed = {'.text', '.data', '.dynamic_data', '.ram0_data', '.cache_ram_data'}
    missing = [name for name, value in sections.items()
               if value['size'] and value['flags'] & 4 and name not in packed]
    if missing:
        raise ValueError('Executable ELF sections omitted by SDK packaging: ' + ', '.join(missing))


def check_elf(path: Path) -> None:
    """读取目标 ELF 并检查打包覆盖。"""
    data = path.read_bytes()
    if len(data) < 52 or data[:6] != b'\x7fELF\x01\x01':
        raise ValueError('Expected a target little-endian ELF32')
    offset = struct.unpack_from('<I', data, 32)[0]
    size, count, strings_index = struct.unpack_from('<HHH', data, 46)
    if size != 40 or not 0 < count <= 4096 or strings_index >= count or offset + size * count > len(data):
        raise ValueError('Invalid ELF section table')
    headers = [struct.unpack_from('<10I', data, offset + i * size) for i in range(count)]
    names = headers[strings_index]
    if names[4] + names[5] > len(data):
        raise ValueError('Invalid ELF string table')
    text = data[names[4]:names[4] + names[5]]
    sections = {}
    for header in headers:
        if header[2] & 2:
            name = text[header[0]:text.index(b'\0', header[0])].decode('ascii')
            sections[name] = {'size': header[5], 'flags': header[2]}
    check_packed_executable_sections(sections)


def prepare(source: Path, output: Path) -> None:
    """只对成对的生成 .ld.vendor 输入补充 JLSP 段，并拒绝越界、链接穿越或原厂锚点变化。"""
    source, output = source.resolve(), output.resolve()
    allowed = (ROOT / 'apps/D_Code/build/out').resolve()
    normal = (ROOT / 'cpu/wl82/sdk.ld').resolve()
    if output != normal and not output.is_relative_to(allowed):
        raise ValueError('Generated linker output must stay in this project')
    if source != output.with_name(output.name + '.vendor') or output.suffix != '.ld':
        raise ValueError('Expected the paired preprocessed .ld.vendor input')
    text = source.read_text(encoding='utf-8')
    marker = '*(.text*)'
    if text.count(marker) != 1:
        raise ValueError('Vendor .text input pattern changed; review linker placement')
    section = '*(.jlsp_dns_code .jlsp_dns_code.*)'
    if '.jlsp_dns_code' in text:
        raise ValueError('Vendor now places JLSP code; review and retire this adapter')
    output.write_text(text.replace(marker, marker + '\n        ' + section),
                      encoding='utf-8', newline='\n')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--input', type=Path)
    parser.add_argument('--output', type=Path)
    parser.add_argument('--elf', type=Path)
    args = parser.parse_args()
    if args.elf and not args.input and not args.output:
        check_elf(args.elf)
        print('ELF executable section coverage PASS')
    elif not args.elf and args.input and args.output:
        prepare(args.input, args.output)
    else:
        parser.error('Use --elf or the --input/--output pair')
