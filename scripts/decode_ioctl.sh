#!/system/bin/sh
echo "=== Decoding 0x80 magic ioctls ==="
# Decode ioctl command: _IOC(dir, magic, nr, size)
# WRITE = 1, READ = 2, READ|WRITE = 3

echo "0x18: dir=WRITE, magic=0x80, nr=0x18, size=0x20 (32)"
echo "0x16: dir=RDWR, magic=0x80, nr=0x16, size=0x18 (24)"  
echo "0x6:  dir=RDWR, magic=0x80, nr=0x6, size=0x10 (16)"
echo "0x1d: dir=WRITE, magic=0x80, nr=0x1d, size=0x10 (16)"
echo "0x5:  dir=RDWR, magic=0x80, nr=0x5, size=0x20 (32)"
echo "0x14: dir=WRITE, magic=0x80, nr=0x14, size=0x10 (16)"
echo "0x2:  dir=WRITE, magic=0x80, nr=0x2, size=0x10 (16)"
echo "0x1b: dir=WRITE, magic=0x80, nr=0x1b, size=0x10 (16)"

echo ""
echo "=== These are NOT Mali kbase (0x67) - MTK custom ==="
echo "=== mmap at 0x41000 is standard Mali ring buffer ==="