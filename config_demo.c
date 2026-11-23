/* Demo fluida: el mismo sistema completo, declarado con la API
 * encadenable (new -> setters -> take). Solo se configura lo que se
 * quiere; el resto son defaults. Hermetico. Uso: ./build/config_demo */
#include <noneos.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0;
#define CHECK(c) do { \
    if (!(c)) { printf("FAIL %d: %s\n", __LINE__, #c); ++failures; } \
} while (0)

static void print_summary(const sys_system_t *s)
{
    size_t i;
    printf("== NoneOS demo config ==\n");
    printf("sysroot : %s\n", s->sysroot);
    printf("hostname: %s (libc global %s, init %d)\n", s->hostname,
           s->libc[0] ? s->libc : "(default)", (int)s->init_system);
    printf("packages: %zu\n", s->npackages);
    for (i = 0; i < s->npackages; ++i) {
        const sys_package_t *p = &s->packages[i];
        const char *be = p->backend == SYS_BACKEND_XBPS ? "xbps" :
                         p->backend == SYS_BACKEND_PKGSRC ? "pkgsrc" : "auto";
        printf("  - %s [%s]%s%s\n", p->name, be,
               p->libc[0] ? " libc=" : "", p->libc[0] ? p->libc : "");
    }
    printf("dysks   : %zu\n", s->ndysks);
    for (i = 0; i < s->ndysks; ++i) {
        const sys_dysk_t *d = &s->dysks[i];
        printf("  - %s -> %s", d->device, d->mountpoint);
        if (d->crypt_uuid[0])
            printf(" [LUKS %s]", d->crypt_uuid);
        printf("\n");
    }
    printf("swapfiles: %zu\n", s->nswapfiles);
    printf("users   : %zu  services: %zu\n", s->nusers, s->nservices);
    printf("bootloader: grub uefi=%d sb=%d kernel=%s\n",
           s->bootloader.is_uefi, s->bootloader.secure_boot.enabled,
           s->bootloader.kernel);
}

int main(void)
{
    static const char *ana_groups[] = {"wheel"};
    sys_package_t *musl_pkgs[2];
    sys_system_t *sys = sys_system_new();
    sys_service_t *sshd;
    sys_user_t *sshd_user;

    sys_system_init_system(
        sys_system_sysroot(
            sys_system_hostname(sys_system_libc(sys, "glibc"), "nonebox"),
            "/mnt/target"),
        SYS_INIT_SYSTEMD);

    /* Paquetes base + kernel con version. */
    CHECK(sys_system_take_package(sys, sys_package_new("base-files")) == 0);
    CHECK(sys_system_take_package(
              sys, sys_package_version(sys_package_new("linux"), "6.9")) == 0);
    CHECK(sys_system_take_package(sys, sys_package_new("bash")) == 0);
    /* Array dinamico de tipo propio: el lote musl de una vez. */
    musl_pkgs[0] = sys_package_libc(sys_package_new("htop"), "musl");
    musl_pkgs[1] = sys_package_libc(sys_package_new("busybox"), "musl");
    CHECK(sys_system_take_packages(sys, musl_pkgs, 2) == 0);
    /* pkgsrc con compilador explicito (bajar de nivel si hace falta). */
    CHECK(sys_system_take_package(
              sys,
              sys_package_compiler(
                  sys_package_backend(sys_package_new("vim"),
                                      SYS_BACKEND_PKGSRC),
                  "cc")) == 0);

    /* Discos: raiz LUKS + ESP clara. */
    CHECK(sys_system_take_dysk(
              sys,
              sys_dysk_pass(
                  sys_dysk_crypt_uuid(
                      sys_dysk_fs(
                          sys_dysk_mountpoint(
                              sys_dysk_new("/dev/mapper/cryptroot"), "/"),
                          "ext4"),
                      "11111111-2222-3333-4444-555555555555"),
                  1)) == 0);
    CHECK(sys_system_take_dysk(
              sys,
              sys_dysk_fs(
                  sys_dysk_mountpoint(sys_dysk_new("/dev/sda1"),
                                      "/boot/efi"),
                  "vfat")) == 0);

    /* Swapfile de 2G. */
    CHECK(sys_system_take_swapfile(
              sys,
              sys_swapfile_size_mb(sys_swapfile_new("/swapfile"), 2048)) == 0);

    /* Usuarios: root pelado + ana con grupo (array de una). */
    CHECK(sys_system_take_user(
              sys,
              sys_user_shell(sys_user_new("root"), "/bin/bash")) == 0);
    CHECK(sys_system_take_user(
              sys,
              sys_user_groups(
                  sys_user_shell(
                      sys_user_uid(sys_user_new("ana"), 1000), "/bin/bash"),
                  ana_groups, 1)) == 0);

    /* SSH tipado: puerto + usuario por copia + raw (encadenado). */
    sshd_user = sys_user_shell(sys_user_new("sshd"), "/bin/sh");
    sshd = S_SSH();
    sshd->port(sshd, 2222).user(sshd, sshd_user).raw(sshd,
                                                      "PermitRootLogin no");
    sys_user_free_heap(sshd_user);
    CHECK(sys_system_take_service(sys, sshd) == 0);

    /* Bootloader: se baja de nivel al embebido y se sigue encadenando. */
    sys_secure_boot_enable(
        sys_bootloader_secure_boot(
            sys_bootloader_uefi(
                sys_bootloader_timeout(
                    sys_bootloader_initrd(
                        sys_bootloader_kernel(sys_system_bootloader(sys),
                                              "/boot/vmlinuz-6.9"),
                        "/boot/initramfs-6.9.img"),
                    5))));

    print_summary(sys);
    sys_system_free(sys);
    free(sys);
    if (failures == 0)
        printf("demo OK\n");
    return failures ? 1 : 0;
}
