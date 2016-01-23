# Cyanurus

Cyanurus is a Unix-like operating system for ARMv7-A.

![Screen Capture](https://cloud.githubusercontent.com/assets/928237/11782406/3c520650-a2b4-11e5-917e-372b7f7cb689.gif)

## How to Run

Cyanurus kernel and rootfs image are available at below link:

https://github.com/redcap97/cyanurus/releases/download/v0.2.0/cyanurus-0.2.0.tar.xz

QEMU is required to run Cyanurus. Please type following command:

```
qemu-system-arm -M vexpress-a9 -m 1G -nographic -drive if=sd,file=rootfs.img,format=raw -kernel cyanurus.elf
```

Cyanurus is also able to run on docker containers. Please type following command:

```
docker run -it --rm redcap97/cyanurus
```

## Build Dependencies

### Required

* Linux
* gmake
* gcc (arm-none-eabi)
* binutils (arm-none-eabi)
* ruby
* qemu-system-arm
* qemu-img
* mkfs.minix

### Optional

* clang
* gdb (arm-none-eabi)

## License

Licensed under the Apache License v2.0.
