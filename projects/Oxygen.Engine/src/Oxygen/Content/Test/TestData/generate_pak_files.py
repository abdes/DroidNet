import struct
import os

#=== PAK Format Constants ===---------------------------------------------------//
MAGIC = b'OXPAK\x00\x00\x00'  # 8 bytes: {'O','X','P','A','K',0,0,0}
FOOTER_MAGIC = b'OXPAKEND'      # 8 bytes: {'O','X','P','A','K','E','N','D'}
VERSION = 1
HEADER_SIZE = 64
FOOTER_SIZE = 64
DIRECTORY_ENTRY_SIZE = 64

# Asset type enum (must match C++ values)
ASSET_TYPE_GEOMETRY = 1
ASSET_TYPE_MESH = 2
ASSET_TYPE_TEXTURE = 3
ASSET_TYPE_SHADER = 4
ASSET_TYPE_MATERIAL = 5
ASSET_TYPE_AUDIO = 6

# Helper to build a valid AssetKey (24 bytes)
def make_asset_key(guid_bytes, variant, version, asset_type, reserved=0):
    assert len(guid_bytes) == 16
    key = bytearray()
    key += guid_bytes
    key += struct.pack('<I', variant)
    key += struct.pack('<B', version)
    key += struct.pack('<B', asset_type)
    key += struct.pack('<H', reserved)
    assert len(key) == 24
    return bytes(key)

def make_asset_guid(asset_index):
    # 16 bytes: 'ASSET' + 3-digit zero-padded index + pad with zeros
    prefix = b'ASSET'
    num = f"{asset_index:03d}".encode('ascii')
    guid = prefix + num
    guid = guid.ljust(16, b'\x00')
    return guid

#=== Utility Functions ===------------------------------------------------------//
def pad_to(f, alignment):
    pos = f.tell()
    pad = (alignment - (pos % alignment)) % alignment
    if pad:
        f.write(b'\x00' * pad)
    return pad

def get_output_path(filename):
    return os.path.join(os.path.dirname(__file__), filename)

#=== PAK File Writers ===-------------------------------------------------------//
def write_empty_valid_pak(path):
    with open(path, 'wb') as f:
        # Header
        f.write(MAGIC)
        f.write(struct.pack('<H', VERSION))
        f.write(struct.pack('<H', 0))  # content_version
        f.write(b'\x00' * 52)
        # No data, no directory
        # Footer
        f.write(struct.pack('<Q', 0))  # directory_offset
        f.write(struct.pack('<Q', 0))  # directory_size
        f.write(struct.pack('<Q', 0))  # asset_count
        f.write(b'\x00' * 24)
        f.write(struct.pack('<Q', 0))  # pak_hash
        f.write(FOOTER_MAGIC)


def write_assets_pak(path, assets):
    """
    assets: list of tuples (key_bytes, data_size, alignment, dependency_keys, compression, reserved0, reserved[20])
    key_bytes: 24 bytes
    dependency_keys: list of AssetKey bytes (each 24 bytes), or []
    """
    asset_offsets = []
    asset_sizes = []
    directory_entry_offsets = []
    def get_asset_type_from_key(key_bytes):
        # Asset type is at offset 21 (see make_asset_key: 16 GUID + 4 variant + 1 version = 21)
        return key_bytes[21]
    def asset_type_char(asset_type):
        # Map asset type to ASCII char
        return {
            ASSET_TYPE_GEOMETRY: b'G',
            ASSET_TYPE_MESH: b'M',
            ASSET_TYPE_TEXTURE: b'T',
            ASSET_TYPE_SHADER: b'S',
            ASSET_TYPE_MATERIAL: b'M',
            ASSET_TYPE_AUDIO: b'A',
        }.get(asset_type, b'?')
    with open(path, 'wb') as f:
        # Header
        f.write(MAGIC)
        f.write(struct.pack('<H', VERSION))
        f.write(struct.pack('<H', 0))
        f.write(b'\x00' * 52)

        # Write each asset, enforcing alignment and recording absolute file offset
        for key_bytes, data_size, alignment, dependency_keys, compression, reserved0, reserved in assets:
            # Align file position for this asset
            pos = f.tell()
            pad = (alignment - (pos % alignment)) % alignment if alignment else 0
            if pad:
                f.write(b'\x00' * pad)
            # Record absolute file offset (after alignment)
            offset_in_file = f.tell()
            asset_offsets.append(offset_in_file)
            # Write asset data: use repeated ASCII char for type
            asset_type = get_asset_type_from_key(key_bytes)
            char = asset_type_char(asset_type)
            print(f"Asset at offset {offset_in_file}: type={asset_type} char={char.decode(errors='replace')} size={data_size} key_bytes={key_bytes.hex()}")
            f.write(char * data_size)
            asset_sizes.append(data_size)

        # Directory offset is after all asset data
        directory_offset = f.tell()
        directory_entries = []
        dependency_blobs = []
        for i, (key_bytes, data_size, alignment, dependency_keys, compression, reserved0, reserved) in enumerate(assets):
            entry_offset = f.tell() + len(directory_entries) * 64 + sum(len(deps) * 24 for deps in dependency_blobs)
            data_offset = asset_offsets[i]
            size = asset_sizes[i]
            dep_count = len(dependency_keys)
            # Build reserved[12] (all zeros)
            reserved12 = b'\x00' * 12
            # Pack directory entry (64 bytes)
            entry = b''
            entry += key_bytes  # 24 bytes
            entry += struct.pack('<Q', entry_offset)  # entry_offset (absolute offset of this entry in file)
            entry += struct.pack('<Q', data_offset)    # data_offset (absolute offset of asset data)
            entry += struct.pack('<I', size)           # size
            entry += struct.pack('<I', alignment)      # alignment
            entry += struct.pack('<H', dep_count)      # dependency_count
            entry += struct.pack('<B', compression)    # compression
            entry += struct.pack('<B', reserved0)      # reserved0
            entry += reserved12                        # reserved[12]
            assert len(entry) == 64
            directory_entries.append(entry)
            dependency_blobs.append(dependency_keys)
        # Write directory entries and dependencies
        for i, (entry, deps) in enumerate(zip(directory_entries, dependency_blobs)):
            entry_offset = f.tell()
            # Unpack the entry to update entry_offset
            key_bytes = entry[:24]
            # entry_offset is at offset 24, so we rebuild the entry with the correct offset
            data_offset = asset_offsets[i]
            size = asset_sizes[i]
            alignment = assets[i][2]
            dep_count = len(deps)
            compression = assets[i][4]
            reserved0 = assets[i][5]
            reserved12 = b'\x00' * 12
            packed_entry = b''
            packed_entry += key_bytes
            packed_entry += struct.pack('<Q', entry_offset)
            packed_entry += struct.pack('<Q', data_offset)
            packed_entry += struct.pack('<I', size)
            packed_entry += struct.pack('<I', alignment)
            packed_entry += struct.pack('<H', dep_count)
            packed_entry += struct.pack('<B', compression)
            packed_entry += struct.pack('<B', reserved0)
            packed_entry += reserved12
            assert len(packed_entry) == 64
            f.write(packed_entry)
            for dep_key in deps:
                f.write(dep_key)
        directory_end = f.tell()
        directory_size = directory_end - directory_offset
        asset_count = len(assets)

        # Footer
        f.write(struct.pack('<Q', directory_offset))
        f.write(struct.pack('<Q', directory_size))
        f.write(struct.pack('<Q', asset_count))
        f.write(b'\x00' * 24)
        f.write(struct.pack('<Q', 0))
        f.write(FOOTER_MAGIC)
        f.flush()
        file_size = f.tell()
        print(f"Wrote {os.path.basename(path)}: size={file_size}, footer_offset={file_size - FOOTER_SIZE}")


def write_simple_assets_pak(path):
    assets = [
        (make_asset_key(make_asset_guid(0), 0, 1, ASSET_TYPE_GEOMETRY), 128, 256, [], 1, 0, b'\x00' * 20),  # Geometry
        (make_asset_key(make_asset_guid(1), 0, 1, ASSET_TYPE_TEXTURE), 64, 256, [], 0, 0, b'\x00' * 20),    # Texture
        (make_asset_key(make_asset_guid(2), 0, 1, ASSET_TYPE_SHADER), 32, 16, [], 0, 0, b'\x00' * 20),     # Shader
        (make_asset_key(make_asset_guid(3), 0, 1, ASSET_TYPE_MATERIAL), 16, 16, [], 0, 0, b'\x00' * 20),   # Material
    ]
    write_assets_pak(path, assets)


def write_complex_assets_pak(path):
    assets = []
    geometry_keys = []
    asset_idx = 0
    for lod in range(3):
        for mesh in range(2):
            guid_bytes = make_asset_guid(asset_idx)
            key = make_asset_key(guid_bytes, 0, 1, ASSET_TYPE_GEOMETRY)
            geometry_keys.append(key)
            assets.append((key, 512 + 128 * lod + 64 * mesh, 256, [], 0, 0, b'\x00' * 20))
            asset_idx += 1
    # Texture asset: large, aligned, depends on first two geometry assets
    guid_bytes = make_asset_guid(asset_idx)
    texture_deps = geometry_keys[:2]
    texture_key = make_asset_key(guid_bytes, 0, 1, ASSET_TYPE_TEXTURE)
    assets.append((texture_key, 1024, 256, texture_deps, 0, 0, b'\x00' * 20))
    asset_idx += 1
    # Shader asset: medium size
    guid_bytes = make_asset_guid(asset_idx)
    shader_key = make_asset_key(guid_bytes, 0, 1, ASSET_TYPE_SHADER)
    assets.append((shader_key, 256, 16, [], 0, 0, b'\x00' * 20))
    asset_idx += 1
    # Material asset: small
    guid_bytes = make_asset_guid(asset_idx)
    material_key = make_asset_key(guid_bytes, 0, 1, ASSET_TYPE_MATERIAL)
    assets.append((material_key, 64, 16, [], 0, 0, b'\x00' * 20))
    write_assets_pak(path, assets)

#=== Main Entrypoint ===--------------------------------------------------------//
if __name__ == '__main__':
    out_empty = get_output_path('EmptyValid.pak')
    out_simple = get_output_path('SimpleAssets.pak')
    out_complex = get_output_path('ComplexAssets.pak')
    write_empty_valid_pak(out_empty)
    write_simple_assets_pak(out_simple)
    write_complex_assets_pak(out_complex)
    print('Test PAK files generated in script directory.')
