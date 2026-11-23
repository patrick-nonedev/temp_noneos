/* mini-init: initramfs /init estatico (musl) que el glue empaqueta como
 * ULTIMO fallback cuando dracut/mkinitfs fallan o no estan.
 *
 * Carga los modulos del root con finit_module(2) desde /modules/*.ko
 * (el packer los deja DESCOMPRIMIDOS con nombres 000-xxx.ko para
 * respetar el orden de deps), monta root=UUID=<uuid> (ext4 lectura
 * directa del superblock; vfat por volume serial) y hace switch_root a
 * /sbin/init del target. Sin userspace dinamico.
 */
#define _GNU_SOURCE
#define _DEFAULT_SOURCE
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>
#include <dirent.h>
#include <errno.h>

#ifndef FINIT_MODULE_NR
/* x86_64: finit_module = 313 (musl no trae wrapper). */
#define FINIT_MODULE_NR 313
#endif

static void wr(const char *s)
{
    (void)!write(2, s, strlen(s));
}

static int load_module(const char *path)
{
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        wr("mini-init: open ");
        wr(path);
        wr("\n");
        return -1;
    }
    if (syscall(FINIT_MODULE_NR, fd, "", 0) != 0) {
        wr("mini-init: finit fail ");
        wr(path);
        wr("\n");
        close(fd);
        return -1;
    }
    close(fd);
    return 0;
}

static void hex2bin(const char *h, unsigned char *o, size_t n)
{
    size_t i;
    for (i = 0; i < n && h[i * 2] && h[i * 2 + 1]; ++i) {
        unsigned char v = 0;
        int k;
        for (k = 0; k < 2; ++k) {
            unsigned char c = (unsigned char)h[i * 2 + k];
            v = (unsigned char)(v << 4);
            if (c >= '0' && c <= '9')
                v = (unsigned char)(v + (c - '0'));
            else if (c >= 'a' && c <= 'f')
                v = (unsigned char)(v + (c - 'a' + 10));
            else if (c >= 'A' && c <= 'F')
                v = (unsigned char)(v + (c - 'A' + 10));
        }
        o[i] = v;
    }
}

/* root uuid lo deja el packer en /modules/.root-uuid (mas simple que
 * parsear el cmdline aki). */
static int read_root_uuid(char *out, size_t cap)
{
    int fd = open("/modules/.root-uuid", O_RDONLY);
    ssize_t n = -1;
    if (fd < 0)
        return -1;
    n = read(fd, out, cap - 1);
    close(fd);
    if (n <= 0)
        return -1;
    out[n] = '\0';
    while (n > 0 && (out[n - 1] == '\n' || out[n - 1] == ' '))
        out[--n] = '\0';
    /* blkid da "xxxx-xxxx-..." (ext) o "3824-3852" (FAT): quitamos los
     * guioncs para que hex2bin funcione sin parsar el cmdline. */
    {
        char *r = out;
        char *w = out;
        while (*r) {
            if (*r != '-') {
                *w++ = *r;
            }
            ++r;
        }
        *w = '\0';
    }
    return out[0] ? 0 : -1;
}

static int ext_match_uuid(int fd, const unsigned char *want)
{
    /* p�Anda FULL superblock: magic en 1080 y s_uuid en 1024+112=1136. */
    unsigned char sb[128];
    if (pread(fd, sb, sizeof(sb), 1024) != (ssize_t)sizeof(sb))
        return 0;
    /* ext2/3/4 magic 0xEF53 en offset 1080 (little endian: 0x53,0xEF) */
    if (sb[1080 - 1024] != 0x53 || sb[1081 - 1024] != 0xEF)
        return 0;
    /* ext4 superblock.s_uuid en offset 112 del superblock. */
    return memcmp(sb + 112, want, 16) == 0;
}

static int vfat_match_uuid(int fd, const unsigned char *want)
{
    unsigned char sb[72];
    if (pread(fd, sb, sizeof(sb), 0) != (ssize_t)sizeof(sb))
        return 0;
    if (sb[3] != 'M' || sb[4] != 'S' || sb[5] != 'D' || sb[6] != 'O' ||
        sb[7] != 'S')
        return 0;
    /* FAT volume serial (4 bytes) en offset 67: es "el UUID corto". */
    return memcmp(sb + 67, want, 4) == 0;
}

/* Fat uuid mostrado por blkid: XXXX-XXXX (hex 4b). Si pasamos 32 hex
 * (formato ext) usa superblock ext. */
static int try_mount_by_uuid(const char *dev, const char *uuid)
{
    int fd;
    unsigned char want[16];
    int is_fat = (strlen(uuid) <= 8);
    memset(want, 0, sizeof(want));
    fd = open(dev, O_RDONLY);
    if (fd < 0)
        return -1;
    hex2bin(uuid, want, sizeof(want));
    if (is_fat && vfat_match_uuid(fd, want)) {
        close(fd);
        return mount(dev, "/newroot", "vfat", 0, NULL) == 0 ? 0 : -1;
    }
    if (!is_fat && ext_match_uuid(fd, want)) {
        close(fd);
        return mount(dev, "/newroot", "ext4", 0, NULL) == 0 ? 0 : -1;
    }
    close(fd);
    return -1;
}

int main(void)
{
    char uuid[128];
    DIR *d;
    struct dirent *de;
    struct stat st;

    mkdir("/proc", 0755);
    mkdir("/sys", 0755);
    mkdir("/dev", 0755);
    mkdir("/newroot", 0755);
    mkdir("/modules", 0755);
    (void)mount("/proc", "/proc", "proc", 0, NULL);
    (void)mount("sysfs", "/sys", "sysfs", 0, NULL);
    (void)mount("devtmpfs", "/dev", "devtmpfs", 0, NULL);
    if (stat("/dev", &st) == 0 && S_ISDIR(st.st_mode))
        chmod("/dev", 0755);

    if (read_root_uuid(uuid, sizeof(uuid)) != 0) {
        wr("mini-init: sin root=UUID\n");
        for (;;)
            pause();
    }

    {
        struct dirent **ents = NULL;
        int n = scandir("/modules", &ents, NULL, alphasort);
        for (int j = 0; j < n; j++) {
            char p[512];
            size_t ln = strlen(ents[j]->d_name);
            if (ln < 3 || strcmp(ents[j]->d_name + ln - 3, ".ko") != 0)
                continue;
            snprintf(p, sizeof(p), "/modules/%s", ents[j]->d_name);
            if (stat(p, &st) != 0 || !S_ISREG(st.st_mode))
                continue;
            load_module(p);
        }
        for (int j = 0; j < n; j++)
            free(ents[j]);
        free(ents);
        /* scandir garantiza orden alfabético; 000-xxx.ko antes de 001-yyy.ko */
    }

    d = opendir("/dev");
    if (!d) {
        wr("mini-init: sin /dev\n");
        for (;;)
            pause();
    }
    while ((de = readdir(d)) != NULL) {
        char p[512];
        size_t ln = strlen(de->d_name);
        if (ln < 3 || ln > 16)
            continue;
        /* nombres de disco/con notch de VirtIO/NVMe/SCSI visibles: vda,
         * sda1, nvme0n1p3, mmcblk0p1... Probamos los block con los
         * matchers (fallan de manera segura ante no-match). */
        snprintf(p, sizeof(p), "/dev/%s", de->d_name);
        if (stat(p, &st) != 0)
            continue;
        if ((strncmp(de->d_name, "vd", 2) == 0) ||
            (strncmp(de->d_name, "sd", 2) == 0) ||
            (strncmp(de->d_name, "nvme", 4) == 0) ||
            (strncmp(de->d_name, "mmcblk", 6) == 0) ||
            (strncmp(de->d_name, "loop", 4) == 0)) {
            if (try_mount_by_uuid(p, uuid) == 0) {
                wr("mini-init: root = ");
                wr(p);
                wr("\n");
                closedir(d);
                goto root_ok;
            }
        }
    }
    closedir(d);
    wr("mini-init: root UUID no encontrado entre los /dev visibles\n");
    for (;;)
        pause();

root_ok:
    chdir("/newroot");
    (void)mount("/newroot", "/", NULL, MS_MOVE, (void *)0);
    chroot(".");
    chdir("/");
    {
        static const char *inis[] = {"/sbin/init", "/usr/sbin/init",
                                     "/bin/init", NULL};
        int i;
        char *argv[2] = {(char *)"init", NULL};
        char *env[] = {
            "PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:"
            "/sbin:/bin",
            "HOME=/",
            NULL,
        };
        for (i = 0; inis[i]; ++i) {
            execve(inis[i], (char *const *)argv, (char *const *)env);
        }
    }
    wr("mini-init: no se pudo exec /sbin/init\n");
    for (;;)
        pause();
}
