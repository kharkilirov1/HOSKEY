import struct, sys

path = r'C:\Users\Kharki\DevEcoStudioProjects\HOSKEY\entry\src\main\resources\rawfile\dict\ru.hoskey.hosdict'
data = open(path, 'rb').read()
magic, ver, count, msize = struct.unpack_from('<IIII', data, 0)
print(f'Magic: 0x{magic:08X}, Version: {ver}, Assets: {count}, Manifest: {msize}B')
off = 16 + msize
total_model_bytes = 0
for i in range(count):
    kl = struct.unpack_from('<I', data, off)[0]; off += 4
    key = data[off:off+kl].decode('utf-8', errors='replace'); off += kl
    fl = struct.unpack_from('<I', data, off)[0]; off += 4
    fmt = data[off:off+fl].decode('utf-8', errors='replace'); off += fl
    ao, sz = struct.unpack_from('<QQ', data, off); off += 16
    blob = data[ao:ao+min(sz,16)]
    magic_str = ''
    if len(blob) >= 8 and blob[4:8] == b'MSL2': magic_str = 'MSL2'
    elif len(blob) >= 8 and blob[4:8] == b'TFL3': magic_str = 'TFL3'
    elif len(blob) >= 4: magic_str = blob[:4].hex()
    is_model = 'model' in fmt or 'mindspore' in fmt or any(k in key for k in ('ranking.','neural.','autocc.','tap.','emoji.','rnnt.'))
    marker = ' <<< MODEL' if is_model else ''
    if is_model: total_model_bytes += sz
    print(f'  [{i:2d}] {key:60s} | {fmt:25s} | {sz:>10,}B | magic={magic_str:8s}{marker}')

print(f'\nTotal size: {len(data):,}B ({len(data)/1024/1024:.1f}MB)')
print(f'Model assets: {total_model_bytes:,}B ({total_model_bytes/1024/1024:.1f}MB)')
