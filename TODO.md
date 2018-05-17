#### v0.0.1
##### Kernel
- [ ] Make syscalls safe (currently the user passed pointers are not checked)
- [ ] Use array of pointers to `struct file` instead of array of `struct file` in process structure (useful for duplicating file descriptors)
- [ ] Implement `fcntl` syscall
- [ ] Implement time functions

##### File systems
- [ ] Support tar archive in ramdisk
- [ ] Make ext2 more robust and reliable
- [ ] Redesign procfs to be more dynamic

##### Devices
- [ ] Implement DMA support
- [ ] Add DMA support in ATA driver

##### aqbox
- [ ] Implement a more useful shell (ksh for example)
- [ ] Add support for other utilities and make the current ones robust

##### NuklearUI
- [ ] Work on client/server model
- [ ] Implement a terminal emulator
