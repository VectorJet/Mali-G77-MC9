#!/system/bin/sh
# Strace chrome privileged process
/data/data/com.termux/files/usr/bin/strace -f -e trace=ioctl -p 16071 2>&1 | head -100