/* Demo minimalista fluida: musl + dinit + limine, sin LUKS.
 * Uso: ./build/minimal_demo */
#include <noneos.h>

#include <stdio.h>
#include <stdlib.h>

static int failures = 0;
#define CHECK(c) do { \
    if (!(c)) { printf("FAIL %d: %s\n", __LINE__, #c); ++failures; } \
} while (0)

int main(void)
{
    /* Lote de paquetes de una vez (array de tipo propio). */
    sys_package_t *pkgs[5];
    sys_system_t *sys = sys_system_new();
    size_t i;

    sys_system_init_system(
        sys_system_sysroot(
            sys_system_hostname(sys_system_libc(sys, "musl"), "minibox"),
            "/mnt/minimal"),
        SYS_INIT_DINIT);

    /* Kernel + escritorio minimo musl (heredan la libc global). */
    pkgs[0] = sys_package_new("linux");
    pkgs[1] = sys_package_new("base-devel");
    pkgs[2] = sys_package_new("vim");
    pkgs[3] = sys_package_new("foot");
    pkgs[4] = sys_package_new("dwl");
    pkgs[5] = sys_package_new("NetworkManager");
    CHECK(sys_system_take_packages(sys, pkgs, 5) == 0);

    /* Sin LUKS: raiz + ESP en claro. */
    CHECK(sys_system_take_dysk(
              sys,
              sys_dysk_pass(
                  sys_dysk_fs(
                      sys_dysk_mountpoint(sys_dysk_new("/dev/sda2"), "/"),
                      "ext4"),
                  1)) == 0);
    CHECK(sys_system_take_dysk(
              sys,
              sys_dysk_fs(
                  sys_dysk_mountpoint(sys_dysk_new("/dev/sda1"),
                                      "/boot/efi"),
                  "vfat")) == 0);

    /* Usuario con sh. */
    CHECK(sys_system_take_user(
              sys,
              sys_user_shell(sys_user_uid(sys_user_new("user"), 1000),
                             "/bin/sh")) == 0);

    /* SSH tipado (encadenado). */
    {
        sys_service_t *sshd = S_SSH();
        sshd->port(sshd, 22).raw(sshd, "PermitRootLogin no");
        CHECK(sys_system_take_service(sys, sshd) == 0);
    }

    /* Limine UEFI sin Secure Boot (bajando al embebido). */
    sys_bootloader_uefi(
        sys_bootloader_timeout(
            sys_bootloader_initrd(
                sys_bootloader_kernel(
                    sys_bootloader_type(sys_system_bootloader(sys),
                                        SYS_BL_LIMINE),
                    "/boot/vmlinuz"),
                "/boot/initramfs.img"),
            3));

    printf("== NoneOS minimal demo ==\n");
    printf("host %s, libc %s, init dinit, bl limine\n", sys->hostname,
           sys->libc);
    for (i = 0; i < sys->npackages; ++i)
        printf("  pkg: %s\n", sys->packages[i].name);
    for (i = 0; i < sys->ndysks; ++i)
        printf("  dysk: %s -> %s\n", sys->dysks[i].device,
               sys->dysks[i].mountpoint);
    printf("  user: %s shell=%s\n", sys->users[0].name,
           sys->users[0].shell);

    sys_system_free(sys);
    free(sys);
    if (failures == 0)
        printf("minimal demo OK\n");
    return failures ? 1 : 0;
}
