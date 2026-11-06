/* install_demo.c — instalacion NoneOS REAL de una particion ya
 * montada. A diferencia de minimal_demo.c (que solo valida la API),
 * este fichero TERMINA llamando a apply_system_to(): que instala
 * de verdad paquetes (xbps/Void), usuarios, servicios, fstab,
 * bootloader y generaciones sobre el sysroot.
 *
 * Preparacion previa (manual; el instalador NO particiona ni formatea;
 * el disco /dev/vda1 (ESP) y /dev/vda2 (/) deben estar FORMATEADOS y
 * MONTADOS con sysroot igual a /mnt):
 *
 *   fdisk /dev/vda            # p1 ESP 512M (tipo EF), p2 resto (tipo 83)
 *   mkfs.vfat -F32 /dev/vda1
 *   mkfs.ext4 /dev/vda2
 *   mount /dev/vda2 /mnt
 *   mkdir -p /mnt/boot/efi && mount /dev/vda1 /mnt/boot/efi
 *
 * Para Limine UEFI el instalador copia BOOTX64.EFI de la MISMA live
 * (/usr/share/limine/BOOTX64.EFI), asi que en la live de Arch:
 *   pacman -S limine
 *
 * Uso (root):
 *   noneos-install [sysroot] [rootdev] [efidev]
 *     default: /mnt /dev/vda2 /dev/vda1
 *   ./noneos-install /mnt /dev/vda2 /dev/vda1
 */
#include "noneos.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv)
{
    const char *sysroot = argc > 1 ? argv[1] : "/mnt";
    const char *rootdev = argc > 2 ? argv[2] : "/dev/vda2";
    const char *espdev = argc > 3 ? argv[3] : "/dev/vda1";
    int rc;

    /* Void base: init ruint + red + arranque. "linux6.12" evita el
     * meta 'linux' que arrastra todo el firmware (~600MB). */
    static const pkg_t pkgs[] = {
        {.name = "glibc"},
        {.name = "base-files"},
        {.name = "coreutils"},
        {.name = "util-linux"},
        {.name = "bash"},
        {.name = "linux6.12"},
        {.name = "runit"},
        {.name = "runit-void"},
        {.name = "dhcpcd"},
        {.name = "e2fsprogs"},
        {.name = "dosfstools"},
    };
    static const user_t users[] = {
        {.name = "user", .shell = "/bin/sh"},
    };
    dysk_t ds[2];
    bootloader_cfg_t bl;
    system_t sys;

    ds[0] = (dysk_t){0};
    ds[0].name = rootdev;
    ds[0].mountpoint = "/";
    ds[0].filesystem = "ext4";
    ds[1] = (dysk_t){0};
    ds[1].name = espdev;
    ds[1].mountpoint = "/boot/efi";
    ds[1].filesystem = "vfat";

    memset(&bl, 0, sizeof(bl));
    bl.bootloader = BOOT_LIMINE;
    bl.firmware = UEFI;
    bl.install_path = "/boot/efi";
    bl.kernel = "/boot/vmlinuz";
    bl.ramdisk = "/boot/initramfs.img";
    bl.timeout = 3;

    sys = create_system();
    sys = system_provider(sys, (provider_t){.manager = M_XBPS});
    sys = system_init(sys, INIT_RUNIT);
    sys = system_hostname(sys, "noneos-box");
    sys = system_libc(sys, LIBC_GLIBC);
    sys = system_bootloader(sys, bl);
    sys = system_users(sys, users, 1);
    sys = system_packages(sys, pkgs,
                          sizeof(pkgs) / sizeof(pkgs[0]));
    sys = system_dysks(sys, ds, 2);

    printf("== NoneOS install: sysroot=%s root=%s esp=%s ============================================================================\n",
           sysroot, rootdev, espdev);
    rc = apply_system_to(&sys, sysroot);
    free_system(&sys);
    if (rc != 0) {
        fprintf(stderr, "install: FALLO (ver logs CLOG arriba)\n");
        return 1;
    }
    printf("instalacion OK: desmonta /mnt y reinicia\n");
    return 0;
}
