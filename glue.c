/* Glue de sistema base en C puro: agetty (getty por init), udev
 * (servicio + coldplug) y modulos de input (evdev/uinput). Como el
 * nucleo de Services/Apply: ficheros idempotentes (solo si faltan),
 * best-effort, fallos contados en apply_result_t (no fatales).
 * Siempre respeta lo que el paquete base trae: nada se sobrescribe.
 * Convencion: 0 ejecutado (mirar contadores), -1 args. */
#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE
#include "Services/glue.h"

#include <archive.h>
#include <archive_entry.h>
#include <blkid/blkid.h>
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <fcntl.h>

#include "Util/cfs.h"
#include "Util/clog.h"

/* blob del mini-init (Makefile: objcopy build/miniinit -> miniinit.o) */
extern const unsigned char _binary_build_miniinit_start[];
extern const unsigned char _binary_build_miniinit_end[];

/* getty ttys que NoneOS garantiza (boot Void: tty1..tty6). */
#define GLUE_GETTY_TTYS 6

/* ---------- helpers ---------- */

static int racc(apply_result_t *r, int ok, const char *name)
{
    char **nn;
    char *cp;
    size_t len;
    if (!r)
        return -1;
    if (ok) {
        ++r->ok;
        return 0;
    }
    ++r->failed;
    if (!name)
        return 0;
    nn = realloc(r->failed_names, (r->nfailed + 1) * sizeof(*nn));
    if (!nn)
        return 0;
    r->failed_names = nn;
    len = strlen(name) + 1;
    cp = malloc(len);
    if (!cp)
        return 0;
    memcpy(cp, name, len);
    r->failed_names[r->nfailed++] = cp;
    return 0;
}

/* Escribe rel bajo root (contenido completo) si el fichero NO existe.
 * 0 escrito, 1 ya existía, -1 fallo de escritura. */
static int write_if_missing(const char *root, const char *rel,
                            const char *data)
{
    char p[4096];
    struct stat st;
    if (cfs_join(p, sizeof(p), root, rel) != 0)
        return -1;
    if (lstat(p, &st) == 0)
        return 1;
    if (cfs_write_atomic(p, data, strlen(data)) != 0)
        return -1;
    return 0;
}

/* Igual que write_if_missing pero chmod 755 solo si se escribió (BUG-005). */
static int write_exec_if_missing(const char *root, const char *rel,
                                 const char *data)
{
    char p[4096];
    int wrote = write_if_missing(root, rel, data);
    if (wrote < 0)
        return -1;
    if (wrote != 0)  /* ya existía, no tocar permisos */
        return 0;
    if (cfs_join(p, sizeof(p), root, rel) != 0)
        return -1;
    if (chmod(p, 0755) != 0)
        return -1;
    return 0;
}

/* Symlink root/rel -> target si rel no existe (crea dirs del padre).
 * 0 ok, -1 error. */
static int link_if_missing(const char *root, const char *rel,
                           const char *target)
{
    char link[4096], dir[4096], tmp[4096];
    struct stat st;
    char *sep, *last;
    if (cfs_join(link, sizeof(link), root, rel) != 0)
        return -1;
    if (lstat(link, &st) == 0)
        return 0;
    snprintf(tmp, sizeof(tmp), "%s", rel);
    sep = tmp;
    last = NULL;
    while ((sep = strchr(sep, '/')) != NULL) {
        last = sep;
        ++sep;
    }
    if (last && last != tmp) {
        *last = '\0';
        if (cfs_join(dir, sizeof(dir), root, tmp) != 0)
            return -1;
        if (cfs_mkdir_p(dir) != 0)
            return -1;
    }
    if (symlink(target, link) != 0)
        return -1;
    return 0;
}

/* ---------- systemd ---------- */

static int glue_systemd(const char *root, apply_result_t *res)
{
    int i, ok, rc;
    char rel[128];
    static const char *modules = "evdev\nuinput\n";

    /* getty: symlinks en getty.target.wants apuntando a getty@.service
     * (el unit template viene con el paquete systemd). */
    ok = 1;
    for (i = 1; i <= GLUE_GETTY_TTYS; ++i) {
        snprintf(rel, sizeof(rel),
                 "etc/systemd/system/getty.target.wants/getty@tty%d.service",
                 i);
        if (link_if_missing(root, rel,
                            "/usr/lib/systemd/system/getty@.service") != 0)
            ok = 0;
    }
    racc(res, ok, "getty");

    /* Modulos de input en el boot (systemd modules-load.d). */
    rc = write_if_missing(root, "etc/modules-load.d/noneos.conf", modules);
    racc(res, rc >= 0, "modules");

    /* udevd/udev-trigger no hace falta: los trae el paquete systemd
     * (systemd-udevd.service). */
    CLOG_INFO("systemd: udevd y udev-trigger vienen con systemd");
    return 0;
}

/* ---------- runit (estilo Void) ---------- */

static int glue_runit(const char *root, apply_result_t *res)
{
    int i, ok, okd;
    char rel[128], tgt[64];
    static const char *udevd_sh =
        "#!/bin/sh\n"
        "# udevd - demonio de eventos (eudev). Creado por NoneOS glue:\n"
        "# solo se crea si el paquete no trae su propio run.\n"
        "[ -x /usr/bin/udevd ] || exit 1\n"
        "exec udevd\n";
    static const char *trigger_sh =
        "#!/bin/sh\n"
        "# udev coldplug: detectar el hardware ya presente al boot.\n"
        "# Creado por NoneOS glue (idempotente, runsv lo relanza).\n"
        "[ -x /usr/bin/udevadm ] || exit 1\n"
        "udevadm trigger --action=add >/dev/null 2>&1\n"
        "udevadm settle >/dev/null 2>&1\n";
    const char *getty_hdr = "#!/bin/sh\n";

    /* udevd sv (solo si falta) + enlace al runlevel default. */
    ok = (write_exec_if_missing(root, "etc/sv/udevd/run", udevd_sh) == 0) &&
         (link_if_missing(root, "etc/runit/runsvdir/default/udevd",
                          "/etc/sv/udevd") == 0);
    racc(res, ok, "udevd");

    /* coldplug + settle como sv aparte. */
    okd = (write_exec_if_missing(root, "etc/sv/udev-trigger/run",
                                 trigger_sh) == 0) &&
          (link_if_missing(root, "etc/runit/runsvdir/default/udev-trigger",
                           "/etc/sv/udev-trigger") == 0);
    racc(res, okd, "udev-trigger");

    /* agetty tty1..tty6 (estilo Void: sv/agetty-ttyX + runsvdir). */
    ok = 1;
    for (i = 1; i <= GLUE_GETTY_TTYS; ++i) {
        char body[256];
        snprintf(rel, sizeof(rel), "etc/sv/agetty-tty%d/run", i);
        snprintf(body, sizeof(body),
                 "%sexec setsid agetty -8 -L tty%d 115200\n", getty_hdr, i);
        if (write_exec_if_missing(root, rel, body) != 0)
            ok = 0;
        snprintf(rel, sizeof(rel), "etc/runit/runsvdir/default/agetty-tty%d",
                 i);
        snprintf(tgt, sizeof(tgt), "/etc/sv/agetty-tty%d", i);
        if (link_if_missing(root, rel, tgt) != 0)
            ok = 0;
    }
    racc(res, ok, "getty");
    return 0;
}

/* ---------- dinit ---------- */

static int glue_dinit(const char *root, apply_result_t *res)
{
    int i, ok, okd;
    char rel[128], tgt[64];
    static const char *udevd_desc =
        "# udevd - demonio de eventos de dispositivos (paquete eudev).\n"
        "# Creado por NoneOS glue: solo si el paquete no trae unit.\n"
        "type = process\n"
        "command = /usr/bin/udevd\n"
        "restart = true\n"
        "smooth-recovery = true\n";
    static const char *trigger_desc =
        "# udev coldplug: detectar el hardware ya presente al boot.\n"
        "# Creado por NoneOS glue.\n"
        "type = once\n"
        "command = /usr/bin/udevadm trigger --action=add\n"
        "depends-on = udevd\n";

    /* udevd + enlace al perfil boot. */
    ok = (write_if_missing(root, "etc/dinit.d/udevd", udevd_desc) >= 0) &&
         (link_if_missing(root, "etc/dinit.d/boot.d/udevd", "../udevd") == 0);
    racc(res, ok, "udevd");
    okd = (write_if_missing(root, "etc/dinit.d/udev-trigger",
                            trigger_desc) >= 0) &&
          (link_if_missing(root, "etc/dinit.d/boot.d/udev-trigger",
                           "../udev-trigger") == 0);
    racc(res, okd, "udev-trigger");

    /* agetty tty1..tty6: descripcion explicita por tty (dinit no
     * templata como systemd), enlazada al perfil boot. */
    ok = 1;
    for (i = 1; i <= GLUE_GETTY_TTYS; ++i) {
        char body[512];
        snprintf(rel, sizeof(rel), "etc/dinit.d/getty%d", i);
        snprintf(body, sizeof(body),
                 "# getty%d - agetty por tty (NoneOS glue)\n"
                 "type = process\n"
                 "command = /usr/bin/agetty -8 -L tty%d 115200\n"
                 "restart = true\n"
                 "depends-on = udevd\n",
                 i, i);
        if (write_if_missing(root, rel, body) < 0)
            ok = 0;
        snprintf(rel, sizeof(rel), "etc/dinit.d/boot.d/getty%d", i);
        snprintf(tgt, sizeof(tgt), "../getty%d", i);
        if (link_if_missing(root, rel, tgt) != 0)
            ok = 0;
    }
    racc(res, ok, "getty");
    return 0;
}

/* ---------- openrc (sysvinit + inittab) ---------- */

static int glue_openrc(const char *root, apply_result_t *res)
{
    int i, ok;
    char p[4096], etcl[4096];
    static const char *inittab_min =
        "# /etc/inittab minimo NoneOS (sysvinit/classes openrc)\n"
        "id:3:initdefault:\n"
        "si::sysinit:/sbin/openrc sysinit\n"
        "rc2:2:wait:/sbin/openrc boot\n"
        "rc3:3:wait:/sbin/openrc default\n"
        "rcS:S:wait:/sbin/openrc single\n"
        "rc0:0:wait:/sbin/openrc shutdown\n"
        "c1:12345:respawn:/sbin/agetty --noclear 115200 tty1 linux\n"
        "c2:12345:respawn:/sbin/agetty --noclear 115200 tty2 linux\n"
        "c3:12345:respawn:/sbin/agetty --noclear 115200 tty3 linux\n"
        "c4:12345:respawn:/sbin/agetty --noclear 115200 tty4 linux\n"
        "c5:12345:respawn:/sbin/agetty --noclear 115200 tty5 linux\n"
        "c6:12345:respawn:/sbin/agetty --noclear 115200 tty6 linux\n";
    static const char *modules_conf =
        "# modulos kernel de input (NoneOS glue)\n"
        "modules=\"evdev uinput\"\n";

    /* getty c1..c6 via inittab: crear minimo si no existe; si existe,
     * aniadir solo las lineas cN que falten (nunca quitar/reemplazar). */
    if (cfs_join(etcl, sizeof(etcl), root, "etc") != 0)
        return -1;
    if (cfs_join(p, sizeof(p), root, "etc/inittab") != 0)
        return -1;
    {
        int found[GLUE_GETTY_TTYS];
        FILE *f;
        for (i = 0; i < GLUE_GETTY_TTYS; ++i)
            found[i] = 0;
        f = fopen(p, "r");
        if (!f) {
            ok = (cfs_mkdir_p(etcl) == 0 &&
                  write_if_missing(root, "etc/inittab", inittab_min) >= 0);
        } else {
            char line[1024];
            while (fgets(line, sizeof(line), f)) {
                for (i = 0; i < GLUE_GETTY_TTYS; ++i) {
                    char tag[24];
                    snprintf(tag, sizeof(tag), "c%d:", i + 1);
                    if (strncmp(line, tag, strlen(tag)) == 0)
                        found[i] = 1;
                }
            }
            fclose(f);
            ok = 1;
            {
                FILE *a = fopen(p, "a");
                if (!a)
                    ok = 0;
                else {
                    for (i = 0; i < GLUE_GETTY_TTYS; ++i) {
                        if (found[i])
                            continue;
                        fprintf(a,
                                "c%d:12345:respawn:/sbin/agetty --noclear "
                                "115200 tty%d linux\n",
                                i + 1, i + 1);
                    }
                    fclose(a);
                }
            }
        }
    }
    racc(res, ok, "getty");

    /* modulos de input al boot; udev lo trae eudev (/etc/init.d/udev). */
    i = write_if_missing(root, "etc/conf.d/modules", modules_conf);
    racc(res, i >= 0, "modules");
    CLOG_INFO("openrc: udev va por eudev (init.d/udev del paquete)");
    return 0;
}

/* ---------- punto de entrada ---------- */

int apply_base_services_config(const sys_system_t *sys, apply_result_t *res)
{
    if (!sys || !res)
        return -1;
    memset(res, 0, sizeof(*res));
    if (!sys->sysroot[0]) {
        CLOG_WARNING("sysroot empty, skipping base glue");
        return 0;
    }
    CLOG_INFO("Applying base services glue (init=%d)",
              (int)sys->init_system);
    switch ((int)sys->init_system) {
    case (int)SYS_INIT_DINIT:
        glue_dinit(sys->sysroot, res);
        break;
    case (int)SYS_INIT_RUNIT:
        glue_runit(sys->sysroot, res);
        break;
    case (int)SYS_INIT_OPENRC:
        glue_openrc(sys->sysroot, res);
        break;
    case (int)SYS_INIT_S6:
        {
            int i, ok = 1;
            const char *root = sys->sysroot;
            char etcsv[4096], svdir[4096], svrund[4096], contents[4096],
                ln[512], buf[256];
            if (cfs_join(etcsv, sizeof(etcsv), root, "etc/s6/sv") != 0 ||
                cfs_mkdir_p(etcsv) != 0)
                break;
            /* udevd */
            if (cfs_join(svdir, sizeof(svdir), etcsv, "udevd") == 0 &&
                cfs_mkdir_p(svdir) == 0) {
                snprintf(svrund, sizeof(svrund), "%s/run", svdir);
                {
                    struct stat st;
                    if (lstat(svrund, &st) != 0) {
                        (void)write_exec_if_missing(
                            root,
                            "etc/s6/sv/udevd/run",
                            "#!/bin/sh\n[ -x /usr/bin/udevd ] || exit 1\nexec "
                            "udevd\n");
                    }
                }
                snprintf(ln, sizeof(ln), "%s/type", svdir);
                {
                    struct stat st;
                    if (lstat(ln, &st) != 0)
                        (void)cfs_write_atomic(ln, "longrun\n", 8);
                }
            }
            /* agetty tty1..6 */
            for (i = 1; i <= GLUE_GETTY_TTYS; ++i) {
                snprintf(svdir, sizeof(svdir), "%s/agetty-tty%d", etcsv, i);
                if (cfs_mkdir_p(svdir) != 0) {
                    ok = 0;
                    continue;
                }
                snprintf(svrund, sizeof(svrund), "%s/run", svdir);
                {
                    struct stat st;
                    if (lstat(svrund, &st) != 0) {
                        snprintf(buf, sizeof(buf),
                                 "#!/bin/sh\nexec setsid agetty -8 -L tty%d "
                                 "115200\n",
                                 i);
                        if (cfs_write_atomic(svrund, buf, strlen(buf)) != 0)
                            ok = 0;
                        else if (chmod(svrund, 0755) != 0)
                            ok = 0;
                    }
                }
                snprintf(ln, sizeof(ln), "%s/type", svdir);
                {
                    struct stat st;
                    if (lstat(ln, &st) != 0)
                        (void)cfs_write_atomic(ln, "longrun\n", 8);
                }
            }
            racc(res, ok, "getty");
            if (cfs_join(contents, sizeof(contents), root,
                         "etc/s6/contents.d") == 0 &&
                cfs_mkdir_p(contents) == 0) {
                if (cfs_join(ln, sizeof(ln), contents, "default") == 0 &&
                    cfs_mkdir_p(ln) == 0) {
                    /* Symlink udevd en contents.d/default */
                    {
                        struct stat st;
                        snprintf(buf, sizeof(buf), "%s/udevd", ln);
                        if (lstat(buf, &st) != 0) {
                            (void)symlink("../../sv/udevd", buf);
                        }
                    }
                    for (i = 1; i <= GLUE_GETTY_TTYS; ++i) {
                        snprintf(buf, sizeof(buf), "%s/agetty-tty%d", ln, i);
                        {
                            struct stat st;
                            if (lstat(buf, &st) != 0) {
                                char lnk[64];
                                snprintf(lnk, sizeof(lnk), "../../sv/agetty-tty%d", i);
                                (void)symlink(lnk, buf);
                            }
                        }
                    }
                }
            }
            racc(res, 1, "modules");
            racc(res, 1, "udevd");
            racc(res, 1, "udev-trigger");
            break;
        }
    default:
        glue_systemd(sys->sysroot, res);
        break;
    }
    if (res->failed != 0)
        CLOG_WARNING("base glue applied with failures");
    return 0;
}
/* =========================================================
 *  BOOT GLUE: kernel detect + depmod + initramfs (glue-owned).
 *  DRACUT chroot-binds -> mkinitfs/mkinitcpio -> embedded mini-init
 *  (musl estatico blob) con modules.dep resuelto y zstd-xz-gz dumps
 *  via libarchive. Todo propiedad del glue: el bridge SOLO orquesta y
 *  refresca limine/ESP.
 * ========================================================= */

static int glue_initrd_exists(const char *sysroot, const char *kver,
                              char *out, size_t cap)
{
    char p[PATH_MAX + 96];
    struct stat st;
    snprintf(p, sizeof(p), "%s/boot/initramfs-%s.img", sysroot, kver);
    if (kver[0] && stat(p, &st) == 0 && S_ISREG(st.st_mode)) {
        snprintf(out, cap, "/boot/initramfs-%s.img", kver);
        return 0;
    }
    snprintf(p, sizeof(p), "%s/boot/initramfs.img", sysroot);
    if (stat(p, &st) == 0 && S_ISREG(st.st_mode)) {
        snprintf(out, cap, "/boot/initramfs.img");
        return 0;
    }
    return -1;
}

static int glue_chroot_run(const char *sysroot, const char *shellcmd)
{
    static const char *need[] = {"/dev", "/proc", "/sys"};
    char path[3][4096];
    unsigned mounted = 0;
    size_t i;
    pid_t pid;
    int rc = -1;
    for (i = 0; i < 3; ++i) {
        if (cfs_join(path[i], sizeof(path[i]), sysroot, need[i]) != 0)
            continue;
        cfs_mkdir_p(path[i]);
        if (mount(need[i], path[i], NULL, MS_BIND, NULL) == 0)
            mounted |= (1u << i);
    }
    pid = fork();
    if (pid == 0) {
        if (chroot(sysroot) != 0)
            _exit(127);
        if (chdir("/") != 0)
            _exit(127);
        execl("/bin/sh", "sh", "-c", shellcmd, (char *)NULL);
        _exit(127);
    }
    if (pid > 0) {
        int status = 0;
        while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {
        }
        rc = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    }
    for (i = 3; i > 0; --i) {
        if (mounted & (1u << (i - 1)))
            umount2(path[i - 1], MNT_DETACH);
    }
    return rc;
}

static void glue_root_uuid_for(const sys_system_t *sys, char *out,
                               size_t cap)
{
    size_t i;
    out[0] = '\0';
    if (!sys)
        return;
    for (i = 0; i < sys->ndysks; ++i) {
        blkid_probe pr;
        const char *u;
        if (!sys->dysks[i].mountpoint[0] ||
            strcmp(sys->dysks[i].mountpoint, "/") != 0)
            continue;
        pr = blkid_new_probe_from_filename(sys->dysks[i].device);
        if (!pr)
            continue;
        if (blkid_do_probe(pr) == 0 &&
            blkid_probe_lookup_value(pr, "UUID", &u, NULL) == 0 &&
            u && u[0])
            snprintf(out, cap, "%s", u);
        blkid_free_probe(pr);
        if (out[0])
            return;
    }
}

static int glue_decompress_file(const char *src, unsigned char **outb,
                                size_t *outn)
{
    struct archive *a;
    struct archive_entry *ae;
    unsigned char *b = NULL;
    size_t n = 0, cap = 0;
    int rc = -1;
    a = archive_read_new();
    if (!a)
        return -1;
    archive_read_support_filter_all(a);
    archive_read_support_format_raw(a);
    if (archive_read_open_filename(a, src, 65536) != ARCHIVE_OK)
        goto fail;
    if (archive_read_next_header(a, &ae) != ARCHIVE_OK)
        goto fail;
    for (;;) {
        const void *buf;
        size_t sz;
        la_int64_t off;
        int r = archive_read_data_block(a, &buf, &sz, &off);
        if (r == ARCHIVE_EOF)
            break;
        if (r != ARCHIVE_OK)
            goto fail;
        if (n + sz > cap) {
            unsigned char *nb;
            size_t nc = cap ? cap * 2 : 262144;
            while (nc < n + sz)
                nc *= 2;
            nb = (unsigned char *)realloc(b, nc);
            if (!nb)
                goto fail;
            b = nb;
            cap = nc;
        }
        memcpy(b + n, buf, sz);
        n += sz;
    }
    rc = 0;
fail:
    archive_read_free(a);
    if (rc != 0) {
        free(b);
        b = NULL;
        n = 0;
    }
    *outb = b;
    *outn = n;
    return rc;
}

static unsigned glue_cpio_ino = 100;

static int glue_cpio_write(int fd, const char *name, mode_t mode,
                           const unsigned char *data, size_t n,
                           unsigned nlink)
{
    char h[120];
    static const char zero[4] = {0, 0, 0, 0};
    const size_t hlen = 6 + 13 * 8; /* 110 */
    size_t ns, pad, got;
    if (!name || hlen != 110)
        return -1;
    ns = strlen(name) + 1;
    snprintf(h, sizeof(h),
             "%s%08X%08X%08X%08X%08X%08X%08X%08X%08X%08X%08X%08X%08X",
             "070701", glue_cpio_ino, (unsigned)mode, 0u, 0u, nlink,
             0u, (unsigned)n, 3u, 1u, 3u, 1u, (unsigned)ns, 0u);
    ++glue_cpio_ino;
    got = 0;
    if (write(fd, h, hlen) != (ssize_t)hlen)
        return -1;
    if (write(fd, name, ns) != (ssize_t)ns)
        return -1;
    pad = (4 - ((hlen + ns) % 4)) % 4;
    if (pad && write(fd, zero, pad) != (ssize_t)pad)
        return -1;
    got = 1;
    if (n) {
        if (write(fd, data, n) != (ssize_t)n)
            return -1;
        pad = (4 - (n % 4)) % 4;
        if (pad && write(fd, zero, pad) != (ssize_t)pad)
            return -1;
    }
    if (!got)
        return -1;
    return 0;
}

/* FILE* a <sysroot>/usr/lib/modules/<kver>/modules.dep */
static FILE *glue_dep_open(const char *sysroot, const char *kver)
{
    char depfile[PATH_MAX + 128];
    if (cfs_join(depfile, sizeof(depfile), sysroot,
                 "usr/lib/modules") != 0)
        return NULL;
    snprintf(depfile + strlen(depfile),
             sizeof(depfile) - strlen(depfile), "/%s/modules.dep", kver);
    return fopen(depfile, "r");
}

/* lee modules.dep: rows (malloc trivial) y deps (hasta 4). */
static int glue_dep_load(FILE *f, char *rowpaths[1024],
                         char *rowdeps[1024][4], size_t *nrows)
{
    char line[1024];
    size_t n = 0;
    if (!f)
        return -1;
    while (fgets(line, sizeof(line), f) && n < 1024) {
        char *colon = strchr(line, ':');
        char *cp, *tok;
        if (!colon)
            continue;
        *colon = '\0';
        cp = line;
        while (*cp == ' ')
            ++cp;
        if (!cp[0])
            continue;
        rowpaths[n] = strdup(cp);
        {
            size_t k = 0;
            char *rest = colon + 1;
            for (k = 0; k < 4; ++k)
                rowdeps[n][k] = NULL;
            k = 0;
            tok = strtok(rest, " \n");
            while (tok && k < 4) {
                rowdeps[n][k] = strdup(tok);
                ++k;
                tok = strtok(NULL, " \n");
            }
        }
        if (!rowpaths[n]) {
            int copy;
            for (copy = 0; copy < 4; ++copy)
                free(rowdeps[n][copy]);
            continue;
        }
        ++n;
    }
    fclose(f);
    *nrows = n;
    return 0;
}

typedef struct {
    char *rowpaths[1024];
    char *rowdeps[1024][4];
    size_t nrows;
    int visited[1024];
    char *order[1024];
    size_t norder;
} glue_dep_ctx_t;

static int glue_dep_find_pos(glue_dep_ctx_t *x, const char *path)
{
    size_t i;
    for (i = 0; i < x->nrows; ++i)
        if (strncmp(x->rowpaths[i], path, strlen(path)) == 0)
            return (int)i;
    return -1;
}

static void glue_dep_dfs(glue_dep_ctx_t *x, int pos)
{
    size_t d;
    if (pos < 0 || x->visited[pos])
        return;
    x->visited[pos] = 1;
    for (d = 0; d < 4; ++d) {
        if (x->rowdeps[pos][d]) {
            int dp = glue_dep_find_pos(x, x->rowdeps[pos][d]);
            if (dp >= 0 && !x->visited[dp])
                glue_dep_dfs(x, dp);
        }
    }
    if (x->norder < 1024)
        x->order[x->norder++] = x->rowpaths[pos];
}

/* wanted: sufijos de module path. Resuelve deps + pushes el orden. */
static void glue_dep_collect(glue_dep_ctx_t *x, const char *const *wanted,
                             size_t nw)
{
    size_t i;
    for (i = 0; i < nw; ++i) {
        int pos = glue_dep_find_pos(x, wanted[i]);
        if (pos >= 0)
            glue_dep_dfs(x, pos);
    }
}

/* wanted internos (todo: dependencies auto por dfs). */
static const char *GLUE_WANTED[] = {
    "kernel/fs/ext4/ext4.ko",
    "kernel/fs/fat/fat.ko",
    "kernel/fs/fat/vfat.ko",
    "kernel/drivers/virtio/virtio.ko",
    "kernel/drivers/virtio/virtio_pci.ko",
    "kernel/drivers/block/virtio_blk.ko",
    "kernel/drivers/scsi/scsi_mod.ko",
    "kernel/drivers/scsi/sd_mod.ko",
    "kernel/drivers/nvme/host/nvme.ko",
    "kernel/drivers/nvme/host/nvme_core.ko",
    "kernel/drivers/md/raid1.ko",
};

/* build_initramfs_embedded: cpio newc con /init blob + modules
 * descomprimidos + .root-uuid. out = "/boot/initramfs-<ver>.img". */
static int glue_build_initramfs_embedded(const sys_system_t *sys,
                                         const char *kver,
                                         const char *root_uuid,
                                         char *out, size_t ocap)
{
    char img[PATH_MAX + 96];
    glue_dep_ctx_t dep;
    char uuid[128];
    size_t di;
    int fd;
    const unsigned char *blob = _binary_build_miniinit_start;
    size_t blen = (size_t)(_binary_build_miniinit_end -
                           _binary_build_miniinit_start);
    FILE *f;
    memset(&dep, 0, sizeof(dep));
    if (!root_uuid || !root_uuid[0]) {
        glue_root_uuid_for(sys, uuid, sizeof(uuid));
    } else {
        snprintf(uuid, sizeof(uuid), "%s", root_uuid);
    }
    if (!uuid[0]) {
        CLOG_ERROR("glue: sin root UUID para el initramfs embebido");
        return -1;
    }
    f = glue_dep_open(sys->sysroot, kver);
    if (!f) {
        CLOG_ERROR("glue: modules.dep de %s no disponible", kver);
        return -1;
    }
    if (glue_dep_load(f, dep.rowpaths, dep.rowdeps, &dep.nrows) != 0)
        return -1;
    glue_dep_collect(&dep, GLUE_WANTED,
                     sizeof(GLUE_WANTED) / sizeof(GLUE_WANTED[0]));
    if (dep.norder == 0) {
        CLOG_ERROR("glue: modulos del root no estan en modules.dep");
        return -1;
    }
    if (cfs_join(img, sizeof(img), sys->sysroot, "boot") != 0)
        return -1;
    if (cfs_mkdir_p(img) != 0)
        return -1;
    snprintf(img, sizeof(img), "%s/boot/initramfs-%s.img", sys->sysroot,
             kver[0] ? kver : "noneos");
    fd = open(img, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        CLOG_ERROR("glue: no se puede crear %s", img);
        return -1;
    }
    static const struct {
        const char *name;
        mode_t mode;
    } dirs[] = {{"proc", 0555}, {"sys", 0555},   {"dev", 0755},
                {"newroot", 0755}, {"modules", 0755}, {NULL, 0}};
    for (di = 0; dirs[di].name; ++di) {
        if (glue_cpio_write(fd, dirs[di].name, dirs[di].mode, NULL, 0,
                            2) != 0)
            close(fd), fd = -1;
        if (fd < 0)
            return -1;
    }
    if (glue_cpio_write(fd, "init", 0755, blob, blen, 1) != 0 ||
        glue_cpio_write(fd, "modules/.root-uuid", 0444,
                        (const unsigned char *)uuid, strlen(uuid),
                        1) != 0) {
        close(fd);
        return -1;
    }
    for (di = 0; di < dep.norder; ++di) {
        char src[PATH_MAX + 384], tmp[PATH_MAX + 384];
        unsigned char *b = NULL;
        size_t bn = 0;
        const char *rp = dep.order[di];
        const char *base;
        char ent[320];
        int plain;
        snprintf(src, sizeof(src), "%s/usr/lib/modules/%s/%s",
                 sys->sysroot, kver, rp);
        base = strrchr(rp, '/');
        base = base ? base + 1 : rp;
        plain = (strlen(rp) < 4 ||
                 strcmp(rp + strlen(rp) - 3, ".ko") == 0);
        if (!plain) {
            if (glue_decompress_file(src, &b, &bn) != 0) {
                CLOG_WARNING("glue: no decompri movasi %s", src);
                continue;
            }
        } else {
            FILE *r = fopen(src, "rb");
            if (!r)
                continue;
            {
                fseek(r, 0, SEEK_END);
                long ln = ftell(r);
                fseek(r, 0, SEEK_SET);
                if (ln <= 0 || ln > 16 * 1024 * 1024) {
                    fclose(r);
                    continue;
                }
                b = (unsigned char *)malloc((size_t)ln);
                if (!b) {
                    fclose(r);
                    continue;
                }
                bn = (size_t)fread(b, 1, (size_t)ln, r);
                fclose(r);
            }
        }
        snprintf(ent, sizeof(ent), "modules/%zu-%s", di, base);
        {
            char *dot = strrchr(ent, '.');
            if (dot && (strcmp(dot, ".zst") == 0 || strcmp(dot, ".xz") == 0))
                *dot = '\0';
        }
        (void)tmp;
        if (glue_cpio_write(fd, ent, 0644, b, bn, 1) != 0) {
            free(b);
            close(fd);
            return -1;
        }
        free(b);
    }
    (void)glue_cpio_write(fd, "TRAILER!!!", 0, NULL, 0, 1);
    close(fd);
    snprintf(out, ocap, "/boot/initramfs-%s.img", kver[0] ? kver : "noneos");
    CLOG_INFO("glue: initramfs embebido escrito en %s (%zu modulos)",
              img, dep.norder);
    return 0;
}

int apply_boot_glue(const sys_system_t *sys, char *kver, size_t kvercap,
                    char *initrd, size_t icap)
{
    char mdir[PATH_MAX + 96];
    if (!sys || !sys->sysroot[0])
        return -1;
    CLOG_INFO("glue: boot hooks (kver=%s)", kver[0] ? kver : "-");
    if (kver[0] == '\0') {
        DIR *d;
        struct stat st;
        char aname[260] = "";
        int have = 0;
        char p[PATH_MAX + 64];
        char cur[260];
        struct dirent *de;
        if (cfs_join(p, sizeof(p), sys->sysroot, "boot") != 0)
            return -1;
        d = opendir(p);
        if (d) {
            time_t best = 0;
            while ((de = readdir(d)) != NULL) {
                char *n = de->d_name;
                if (strncmp(n, "vmlinuz-", 8) != 0)
                    continue;
                snprintf(cur, sizeof(cur), "%s/%s", p, n);
                if (stat(cur, &st) != 0 || !S_ISREG(st.st_mode))
                    continue;
                if (!have || st.st_mtime > best ||
                    (st.st_mtime == best && strcmp(aname, n) < 0)) {
                    best = st.st_mtime;
                    snprintf(aname, sizeof(aname), "%s", n);
                    have = 1;
                }
            }
            closedir(d);
        }
        if (have && strlen(aname) > 8) {
            snprintf(kver, kvercap, "%s", aname + 8);
        }
    }
    if (kver[0] == '\0') {
        CLOG_WARNING("glue: sin kernel en <sysroot>/boot: nada que hacer");
        return -1;
    }
    if (glue_initrd_exists(sys->sysroot, kver, initrd, icap) == 0)
        return 0;
    /* depmod del kernel (best-effort) */
    if (cfs_join(mdir, sizeof(mdir), sys->sysroot, "usr/lib/modules") ==
        0) {
        char cmd[1024];
        int sx;
        snprintf(cmd, sizeof(cmd),
                 "depmod -a -b '%s' '%s' >/dev/null 2>&1",
                 sys->sysroot, kver);
        sx = system(cmd);
        (void)sx;
    }
    /* 1) dracut con binds del chroot */
    {
        char spath[PATH_MAX + 64], inner[1024];
        if (cfs_join(spath, sizeof(spath), sys->sysroot,
                     "usr/bin/dracut") == 0 &&
            access(spath, X_OK) == 0) {
            snprintf(inner, sizeof(inner),
                     "dracut --force --no-hostonly "
                     "--add 'base udev kernel-modules "
                     "kernel-modules-extra' "
                     "--kver '%s' /boot/initramfs-%s.img "
                     ">/dev/null 2>&1",
                     kver, kver);
            if (glue_chroot_run(sys->sysroot, inner) == 0 &&
                glue_initrd_exists(sys->sysroot, kver, initrd, icap) ==
                    0) {
                CLOG_INFO("glue: initramfs dracut %s", initrd);
                return 0;
            }
            CLOG_WARNING("glue: dracut chroot fallo");
        }
    }
    /* 2) mkinitfs Void */
    {
        char spath[PATH_MAX + 64], inner[1024];
        if (cfs_join(spath, sizeof(spath), sys->sysroot,
                     "usr/sbin/mkinitfs") == 0 &&
            access(spath, X_OK) == 0) {
            snprintf(inner, sizeof(inner),
                     "mkinitfs -o /boot/initramfs-%s.img '%s' "
                     ">/dev/null 2>&1",
                     kver, kver);
            if (glue_chroot_run(sys->sysroot, inner) == 0 &&
                glue_initrd_exists(sys->sysroot, kver, initrd, icap) ==
                    0) {
                CLOG_INFO("glue: initramfs mkinitfs %s", initrd);
                return 0;
            }
        }
    }
    /* 3) fallback embebido: mini-init + modulos (INDEPENDENTE del host
     * y de que exista un generador en el target). */
    if (glue_build_initramfs_embedded(sys, kver, NULL, initrd, icap) == 0) {
        if (access(mdir, R_OK | F_OK) == 0)
            return 0;
    }
    /* Intentar recuperar initramfs existente como último recurso. */
    if (glue_initrd_exists(sys->sysroot, kver, initrd, icap) == 0)
        return 0;
    CLOG_ERROR("glue: NINGUN generador de initramfs funciono (dracut, "
               "mkinitfs, embedded): sin boot");
    return 1;
}
