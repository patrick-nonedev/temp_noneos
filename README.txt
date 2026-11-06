== NoneOS VM ==
1) Binario listo (sin compilar nada):
     ./noneos-install [sysroot] [rootdev] [espdev]
     default: ./noneos-install /mnt /dev/vda3 /dev/vda2
      FreeBSD D1: fdisk + mkfs + mount manual antes (ver
      cabecera de install_demo.c). Limine UEFI: pacman -S limine
2) Si recompilas NO uses tcc (crash: ZSTD del repodata en
   el link tcc+glibc; verificado con gdb/coredumps).
     glibc: gcc -std=c23 install_demo.c libnone-full-glibc.a\
            -lm -o app
     musl : musl-gcc -static -fno-link-libatomic -std=c23\
            install_demo.c libnone-full-musl.a -o app
3) IMPORTANTE: el .a va SIEMPRE DESPUES del .c/.o.
