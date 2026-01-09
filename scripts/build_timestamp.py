Import("env")
from datetime import datetime

# Generate timestamp once at build start
timestamp_str = datetime.now().strftime("%b %d %Y%H:%M:%S")

# Calculate DJB2 hash (same algorithm as C++ code)
hash_val = 5381
for char in timestamp_str:
    hash_val = ((hash_val << 5) + hash_val) + ord(char)
hash_val = hash_val & 0xFFFFFFFF  # Keep as 32-bit unsigned

build_id = f"{hash_val:08x}"

# Inject as build flag so C++ code and post-build script both use same value
env.Append(CPPDEFINES=[
    ("BUILD_TIMESTAMP_STR", f'\\"Build {build_id}\\"'),
    ("BUILD_ID", f"0x{build_id}")
])

# Store in environment for post-build script
env["FIRMWARE_BUILD_ID"] = build_id
