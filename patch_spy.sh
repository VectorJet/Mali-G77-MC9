sed -i 's/int ioctl(int fd, unsigned long request, ...)/int ioctl(int fd, int request, ...)/g' src/kbase/ioctl_spy.c
