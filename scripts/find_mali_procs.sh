#!/system/bin/sh
# Run a simple GL app to capture init sequence
cd /data/data/com.termux/files/home

# Find a fresh process using mali0
for pid in $(ls /proc | sort -n); do
    if [ -d "/proc/$pid" ]; then
        mali_fd=$(ls -la /proc/$pid/fd 2>/dev/null | grep -c "/dev/mali0")
        if [ "$mali_fd" -gt 0 ]; then
            cmdline=$(cat /proc/$pid/cmdline 2>/dev/null | tr '\0' ' ')
            echo "PID $cmdline"
        fi
    fi
done | head -10