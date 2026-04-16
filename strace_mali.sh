#!/system/bin/sh
/data/data/com.termux/files/usr/bin/strace -f -e trace=openat,mmap,ioctl,write -p 29356 2>&1 | head -300