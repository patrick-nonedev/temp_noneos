/* Nucleo C de Configure. Espejo de noneos.cpp (mismos formatos, mismos
 * fallos altos). Sin PLOG (CLOG). */
#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE
#include "configure.h"

#include <blkid/blkid.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "Services/glue.h"
#include "Services/apply.h"
#include "Services/Crypt/crypt.h"
#include "Services/SecureBoot/secureboot.h"
#include "Services/Swapfile/swapfile.h"
#include "Pkg/pkgman.h"
#include "Pkg/libc.h"
#include "Pkg/rootman.h"
#include "Util/cfs.h"
#include "Util/clog.h"

/* Provisioning de limine INDEPENDIENTE DEL HOST:(pkg del target) ->
 * (bin del host) -> descarga pinneada del release oficial con
 * xbps_fetch_file_dest (libfetch de libxbps) + verificacion sha256 +
 * extraccion con libarchive (BOOTX64.EFI / limine-bios.sys). */
#include <archive.h>
#include <archive_entry.h>
#include <xbps.h>

#define LIMINE_URL \
    "https://github.com/Limine-Bootloader/Limine/releases/download/" \
    "v12.9.6/limine-binary.tar.xz"
#define LIMINE_SHA256 \
    "939f670e0831cc940453fd3a0ddfb2b324c0b4e3c5d0e741c190aa0d13df0103"

struct configure {
    char err[512];
};

static void set_err(configure_t *cfg, const char *msg)
{
    if (cfg && msg)
        snprintf(cfg->err, sizeof(cfg->err), "%s", msg);
}

const char *configure_last_error(const configure_t *cfg)
{
    if (!cfg || !cfg->err[0])
        return "ok";
    return cfg->err;
}

configure_t *configure_new(void)
{
    return (configure_t *)calloc(1, sizeof(configure_t));
}

void configure_free(configure_t *cfg)
{
    free(cfg);
}

/* ---------- fs basico ---------- */

static int path_exists(const char *p)
{
    struct stat st;
    return lstat(p, &st) == 0;
}

/* Buffer creciente para fstab/crypttab (muchos discos no truncan). */
typedef struct {
    char *data;
    size_t len;
    size_t cap;
    int oom;
} growbuf_t;

static int grow_reserve(growbuf_t *b, size_t extra)
{
    char *nd;
    size_t nc;
    if (b->oom)
        return -1;
    if (b->len + extra + 1 <= b->cap)
        return 0;
    nc = b->cap ? b->cap * 2 : 1024;
    while (nc < b->len + extra + 1)
        nc *= 2;
    nd = (char *)realloc(b->data, nc);
    if (!nd) {
        b->oom = 1;
        return -1;
    }
    b->data = nd;
    b->cap = nc;
    return 0;
}

static int grow_add(growbuf_t *b, const char *s)
{
    size_t n = strlen(s ? s : "");
    if (grow_reserve(b, n) != 0)
        return -1;
    memcpy(b->data + b->len, s, n);
    b->len += n;
    b->data[b->len] = '\0';
    return 0;
}

static void grow_free(growbuf_t *b)
{
    free(b->data);
    memset(b, 0, sizeof(*b));
}

static int path_is_dir(const char *p)
{
    struct stat st;
    if (lstat(p, &st) != 0)
        return 0;
    return S_ISDIR(st.st_mode) != 0;
}

/* ---------- ds_create (FHS interno) ---------- */

typedef struct {
    char name[256];
    int is_dir;
    int is_link;
    char target[1024];
    mode_t mode;
} ds_entry_t;



/* ---------- dysks / fstab / crypttab ---------- */

static int get_uuid(const char *device, char *out, size_t cap)
{
    blkid_probe pr;
    const char *data = NULL;
    size_t len = 0;
    if (cap)
        out[0] = '\0';
    if (!device || !device[0] || !out || cap == 0)
        return -1;
    pr = blkid_new_probe_from_filename(device);
    if (!pr) {
        CLOG_FATAL("Unable to open device %s", device);
        return -1;
    }
    if (blkid_do_safeprobe(pr) != 0) {
        blkid_free_probe(pr);
        CLOG_FATAL("Failed to probe device %s", device);
        return -1;
    }
    if (blkid_probe_lookup_value(pr, "UUID", &data, &len) != 0 ||
        !data || len == 0) {
        blkid_free_probe(pr);
        CLOG_FATAL("Device %s has no UUID (needs mkfs or blkid support)",
                   device);
        return -1;
    }
    if (len >= cap)
        len = cap - 1;
    memcpy(out, data, len);
    out[len] = '\0';
    blkid_free_probe(pr);
    return 0;
}

/* Ordena dysks por profundidad de mountpoint (burbuja). */
static void sort_dysks_by_depth(sys_system_t *sys)
{
    size_t i, j;
    for (i = 0; i < sys->ndysks; ++i) {
        for (j = i + 1; j < sys->ndysks; ++j) {
            size_t da = 0, db = 0, k;
            for (k = 0; sys->dysks[i].mountpoint[k]; ++k)
                if (sys->dysks[i].mountpoint[k] == '/')
                    ++da;
            for (k = 0; sys->dysks[j].mountpoint[k]; ++k)
                if (sys->dysks[j].mountpoint[k] == '/')
                    ++db;
            if (db < da) {
                sys_dysk_t tmp = sys->dysks[i];
                sys->dysks[i] = sys->dysks[j];
                sys->dysks[j] = tmp;
            }
        }
    }
}

static int apply_dysks_config(const sys_system_t *sys, const char *sysroot,
                              char *root_uuid, size_t cap)
{
    growbuf_t fstab, crypttab;
    int have_crypt = 0, have_root = 0;
    size_t i;
    memset(&fstab, 0, sizeof(fstab));
    memset(&crypttab, 0, sizeof(crypttab));
    grow_add(&fstab, "# <file system> <mount point>   <type>  "
                         "<options>       <dump>  <pass>\n");
    grow_add(&crypttab,
             "# <target> <source> <keyfile> <options>\n");
    if (root_uuid)
        root_uuid[0] = '\0';
    sort_dysks_by_depth((sys_system_t *)sys);
    for (i = 0; i < sys->ndysks; ++i) {
        const sys_dysk_t *d = &sys->dysks[i];
        char uuid[128], line[8192], mp[4096], fs_type[64],
            opts[1024];
        char verr[512];
        if (!d->mountpoint[0]) {
            CLOG_FATAL("disk with empty mountpoint");
            return -1;
        }
        {
            size_t j;
            for (j = i + 1; j < sys->ndysks; ++j) {
                if (strcmp(d->mountpoint, sys->dysks[j].mountpoint) ==
                    0) {
                    CLOG_FATAL("duplicate mountpoint %s",
                               d->mountpoint);
                    return -1;
                }
            }
        }
        if (crypt_validate(d->device, d->crypt_uuid, d->crypt_keyfile,
                           sysroot, verr, sizeof(verr)) != 0) {
            CLOG_FATAL("%s", verr[0] ? verr : "invalid crypt config");
            return -1;
        }
        if (get_uuid(d->device, uuid, sizeof(uuid)) != 0)
            return -1;
        if (d->crypt_uuid[0]) {
            char tbuf[128], mn[256], cline[2048];
            crypt_blkid_type(d->device, tbuf, sizeof(tbuf));
            if (strcmp(tbuf, "crypto_LUKS") == 0) {
                CLOG_FATAL("device %s is a locked LUKS container "
                           "(open it with cryptsetup open first)",
                           d->device);
                return -1;
            }
            if (crypt_mapper_name(d->device, mn, sizeof(mn)) != 0) {
                CLOG_FATAL("crypt needs /dev/mapper/* device");
                return -1;
            }
            if (crypt_crypttab_line(mn, d->crypt_uuid,
                                    d->crypt_keyfile,
                                    d->crypt_options, cline,
                                    sizeof(cline)) != 0) {
                CLOG_FATAL("cannot build crypttab line");
                grow_free(&fstab);
                grow_free(&crypttab);
                return -1;
            }
            grow_add(&crypttab, cline);
            have_crypt = 1;
        }
        if (strcmp(d->mountpoint, "/") == 0) {
            if (root_uuid)
                snprintf(root_uuid, cap, "%s", uuid);
            have_root = 1;
        }
        {
            size_t k, n = 0;
            for (k = 0; d->mountpoint[k]; ++k) {
                if (d->mountpoint[k] == ' ') {
                    if (n + 4 >= sizeof(mp)) {
                        CLOG_FATAL("mountpoint escape overflow");
                        return -1;
                    }
                    mp[n++] = '\\', mp[n++] = '0', mp[n++] = '4',
                    mp[n++] = '0';
                } else if (d->mountpoint[k] == '\t') {
                    if (n + 4 >= sizeof(mp)) {
                        CLOG_FATAL("mountpoint escape overflow");
                        return -1;
                    }
                    mp[n++] = '\\', mp[n++] = '0', mp[n++] = '1',
                    mp[n++] = '1';
                } else {
                    if (n + 1 >= sizeof(mp)) {
                        CLOG_FATAL("mountpoint escape overflow");
                        return -1;
                    }
                    mp[n++] = d->mountpoint[k];
                }
            }
            mp[n] = '\0';
        }
        if (!d->fs[0])
            snprintf(fs_type, sizeof(fs_type), "%s",
                     d->type[0] ? d->type : "auto");
        else
            snprintf(fs_type, sizeof(fs_type), "%s", d->fs);
        if (!d->options[0])
            snprintf(opts, sizeof(opts), "defaults");
        else
            snprintf(opts, sizeof(opts), "%s", d->options);
        if (!d->swap) {
            snprintf(line, sizeof(line), "UUID=%s %s %s %s %d %d\n",
                     uuid, mp, fs_type, opts, d->dump, d->pass);
        } else {
            snprintf(line, sizeof(line),
                     "UUID=%s none swap defaults %d %d\n", uuid, d->dump,
                     d->pass);
        }
        grow_add(&fstab, line);
    }
    if (fstab.oom || crypttab.oom) {
        CLOG_FATAL("fstab/crypttab out of memory");
        grow_free(&fstab);
        grow_free(&crypttab);
        return -1;
    }
    if (sys->ndysks > 0 && !have_root)
        CLOG_WARNING("No disk mounted on /: bootloader will have empty root=UUID");
    {
        char path[4096];
        if (cfs_join(path, sizeof(path), sysroot, "etc/fstab") != 0 ||
            cfs_write_atomic(path, fstab.data ? fstab.data : "",
                             fstab.len) != 0) {
            CLOG_FATAL("Cannot write fstab to %s", path);
            grow_free(&fstab);
            grow_free(&crypttab);
            return -1;
        }
    }
    if (have_crypt) {
        char path[4096];
        if (cfs_join(path, sizeof(path), sysroot, "etc/crypttab") != 0 ||
            cfs_write_atomic(path, crypttab.data ? crypttab.data : "",
                             crypttab.len) != 0) {
            CLOG_FATAL("Cannot write crypttab to %s", path);
            grow_free(&fstab);
            grow_free(&crypttab);
            return -1;
        }
        crypt_warn_no_hook(sysroot);
    }
    grow_free(&fstab);
    grow_free(&crypttab);
    return 0;
}

/* ---------- hostname / env ---------- */

static int apply_hostname_config(const sys_system_t *sys,
                                 const char *sysroot)
{
    char path[4096], hosts[2048];
    size_t i;
    if (!sys->hostname[0]) {
        CLOG_INFO("No hostname set, skipping");
        return 0;
    }
    if (strlen(sys->hostname) > 64) {
        CLOG_FATAL("hostname too long (max 64)");
        return -1;
    }
    for (i = 0; sys->hostname[i]; ++i) {
        char c = sys->hostname[i];
        int ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                 (c >= '0' && c <= '9') || c == '-' || c == '.';
        if (!ok) {
            CLOG_FATAL("hostname has invalid character");
            return -1;
        }
    }
    CLOG_INFO("Setting hostname");
    if (cfs_join(path, sizeof(path), sysroot, "etc/hostname") != 0)
        return -1;
    if (cfs_write_atomic(path, sys->hostname,
                         strlen(sys->hostname)) != 0) {
        CLOG_ERROR("Cannot write /etc/hostname");
        return 0; /* best-effort */
    }
    {
        size_t n = strlen(sys->hostname);
        snprintf(hosts, sizeof(hosts),
                 "127.0.0.1\tlocalhost\n::1\t\tlocalhost\n127.0.1.1\t%s\n",
                 sys->hostname);
        (void)n;
    }
    if (cfs_join(path, sizeof(path), sysroot, "etc/hosts") != 0)
        return 0;
    if (cfs_write_atomic(path, hosts, strlen(hosts)) != 0)
        CLOG_ERROR("Cannot write /etc/hosts");
    return 0;
}

static int env_safe_bare(const char *v)
{
    size_t i;
    if (!v || !v[0])
        return 0;
    for (i = 0; v[i]; ++i) {
        char c = v[i];
        int ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                 (c >= '0' && c <= '9') || c == '_' || c == '/' ||
                 c == '.' || c == ':' || c == '+' || c == '-' ||
                 c == '%' || c == '@';
        if (!ok)
            return 0;
    }
    return 1;
}

static int apply_env_config(const sys_system_t *sys, const char *sysroot)
{
    char buf[8192];
    size_t o = 0, i;
    if (sys->nenvs == 0) {
        CLOG_INFO("No global env to set, skipping");
        return 0;
    }
    for (i = 0; i < sys->nenvs; ++i) {
        const char *k = sys->envs[i].key;
        const char *v = sys->envs[i].value;
        size_t kl, vl, j;
        if (!k[0]) {
            CLOG_WARNING("skip env with empty key");
            continue;
        }
        kl = strlen(k);
        vl = strlen(v);
        if (o + kl + 1 + vl + 2 >= sizeof(buf))
            break;
        memcpy(buf + o, k, kl);
        o += kl;
        buf[o++] = '=';
        if (env_safe_bare(v)) {
            memcpy(buf + o, v, vl);
            o += vl;
        } else {
            buf[o++] = '"';
            for (j = 0; j < vl; ++j) {
                char c = v[j];
                if (c == '"' || c == '\\' || c == '$' || c == '`') {
                    if (o + 1 >= sizeof(buf))
                        break;
                    buf[o++] = '\\';
                }
                buf[o++] = c;
            }
            buf[o++] = '"';
        }
        buf[o++] = '\n';
    }
    buf[o] = '\0';
    {
        char path[4096];
        if (cfs_join(path, sizeof(path), sysroot, "etc/environment") != 0)
            return -1;
        if (cfs_write_atomic(path, buf, o) != 0) {
            CLOG_ERROR("Cannot write /etc/environment");
            return 0; /* best-effort */
        }
    }
    return 0;
}

/* ---------- users ---------- */

static const char *shell_to_path(const char *sh)
{
    if (!sh || !sh[0])
        return "/bin/bash";
    if (sh[0] == '/')
        return sh;
    if (strcmp(sh, "bash") == 0)
        return "/bin/bash";
    if (strcmp(sh, "sh") == 0)
        return "/bin/sh";
    {
        static char buf[256];
        /* Sin truncar en silencio: shell >250 = path vacio (useradd
         * fallara alto, no con un path cortado). */
        if (cfs_snprintf(buf, sizeof(buf), "/bin/%s", sh) != 0)
            return "";
        return buf;
    }
}

static int run_wait_argv(char *const argv[])
{
    return cfs_run_wait(argv);
}

static int apply_users_config(const sys_system_t *sys, const char *sysroot)
{
    size_t i;
    if (sys->nusers == 0) {
        CLOG_INFO("No custom users to create");
        return 0;
    }
    CLOG_INFO("Creating users");
    for (i = 0; i < sys->nusers; ++i) {
        const sys_user_t *u = &sys->users[i];
        char *argv[16];
        int n = 0;
        char uid_buf[32] = "";
        const char *shell;
        if (!u->name[0] || strchr(u->name, '/') != NULL ||
            strcmp(u->name, ".") == 0 ||
            strcmp(u->name, "..") == 0) {
            CLOG_WARNING("skip user with bad name");
            continue;
        }
        shell = shell_to_path(u->shell);
        /* uid==0 = automatico salvo root. */
        if (!(u->uid == 0 && strcmp(u->name, "root") != 0))
            snprintf(uid_buf, sizeof(uid_buf), "%u",
                     (unsigned)u->uid);
        argv[n++] = (char *)"useradd";
        argv[n++] = (char *)"-R";
        argv[n++] = (char *)sysroot;
        argv[n++] = (char *)"-m";
        argv[n++] = (char *)"-s";
        argv[n++] = (char *)shell;
        if (uid_buf[0]) {
            argv[n++] = (char *)"-u";
            argv[n++] = uid_buf;
        }
        {
            /* Grupos: -G g1,g2 (solo validos). */
            char groups[1024];
            size_t o = 0, k;
            for (k = 0; k < u->ngroups; ++k) {
                const char *g = u->groups[k];
                size_t gl;
                if (!g[0] || strchr(g, '/') != NULL ||
                    strchr(g, ',') != NULL)
                    continue;
                gl = strlen(g);
                if (o > 0) {
                    if (o + 1 >= sizeof(groups))
                        break;
                    groups[o++] = ',';
                }
                if (o + gl >= sizeof(groups))
                    break;
                memcpy(groups + o, g, gl);
                o += gl;
            }
            groups[o] = '\0';
            if (o > 0) {
                argv[n++] = (char *)"-G";
                argv[n++] = groups;
            }
        }
        argv[n++] = (char *)u->name;
        argv[n] = NULL;
        if (run_wait_argv(argv) == 0)
            CLOG_INFO("Created user");
        else
            CLOG_WARNING("useradd failed, continuing with next user");
    }
    return 0;
}

/* ---------- bootloader ---------- */

static void escape_grub_label_c(const char *in, char *out, size_t cap)
{
    size_t o = 0;
    if (!in)
        in = "";
    if (cap == 0)
        return;
    for (; *in && o + 2 < cap; ++in) {
        if (*in == '"' || *in == '\\' || *in == '$' || *in == '`')
            out[o++] = '\\';
        out[o++] = *in;
    }
    out[o] = '\0';
}

static int configure_grub(const sys_system_t *sys, const char *sysroot,
                          const char *root_uuid)
{
    char cfg_path[4096], escaped[512], kern[4096], initrd[1024],
        extra[4100], buf[16384];
    const sys_bootloader_t *bl = &sys->bootloader;
    CLOG_INFO("Configuring grub at %s", cfg_path);
    if (cfs_join(cfg_path, sizeof(cfg_path), sysroot,
                 "boot/grub/grub.cfg") != 0)
        return -1;
    escape_grub_label_c(bl->label, escaped, sizeof(escaped));
    if (!bl->kernel[0])
        snprintf(kern, sizeof(kern), "/boot/vmlinuz");
    else
        snprintf(kern, sizeof(kern), "%s", bl->kernel);
    if (!bl->initrd[0])
        snprintf(initrd, sizeof(initrd), "/boot/initramfs.img");
    else
        snprintf(initrd, sizeof(initrd), "%s", bl->initrd);
    if (bl->cmdline[0])
        snprintf(extra, sizeof(extra), " %s", bl->cmdline);
    else
        snprintf(extra, sizeof(extra), " rw quiet");
    if (root_uuid && root_uuid[0])
        snprintf(buf, sizeof(buf),
                 "set timeout=%u\nset default=%d\n"
                 "search --fs-uuid --set=root %s\n"
                 "menuentry \"%s\" {\nlinux %s root=UUID=%s%s\n"
                 "initrd %s\n}\n",
                 bl->timeout, bl->default_entry, root_uuid, escaped,
                 kern, root_uuid, extra, initrd);
    else
        snprintf(buf, sizeof(buf),
                 "set timeout=%u\nset default=%d\n"
                 "menuentry \"%s\" {\nlinux %s%s\ninitrd %s\n}\n",
                 bl->timeout, bl->default_entry, escaped, kern, extra,
                 initrd);
    if (cfs_write_atomic(cfg_path, buf, strlen(buf)) != 0) {
        CLOG_FATAL("Failed to write grub.cfg");
        return -1;
    }
    return 0;
}

/* Kernel mas reciente en <sysroot>/boot (vmlinuz-<ver>, mtime, empate
 * lexical max). Devuelve kpath "/boot/vmlinuz-<ver>" y su version; si
 * bl->kernel apunta a un fichero real, ese gana (initrd del mismo). */
/* cmdline comun para limine.conf: root=UUID=<uuid> [+ cmdline user] o
 * "rw quiet" si no hay nada. Devuelve 1 si hay UUID. */
static int limc_cmdline(const sys_bootloader_t *bl, const char *root_uuid,
                        char *cmd, size_t cap)
{
    const char *extra = bl->cmdline[0] ? bl->cmdline : "";
    if (root_uuid && root_uuid[0]) {
        snprintf(cmd, cap, "root=UUID=%s%s%s", root_uuid,
                 extra[0] ? " " : "", extra);
        return 1;
    }
    snprintf(cmd, cap, "%s%s", extra, extra[0] ? "" : "rw quiet");
    return 0;
}

/* Extrae un miembro del tarball limine-binary (por basename) a dst. */
static int limine_extract_member(const char *tarp, const char *member,
                                 const char *dst)
{
    struct archive *a;
    struct archive_entry *ae;
    int found = 0;
    a = archive_read_new();
    if (!a)
        return -1;
    archive_read_support_format_tar(a);
    archive_read_support_filter_all(a);
    if (archive_read_open_filename(a, tarp, 10240) != ARCHIVE_OK) {
        archive_read_free(a);
        return -1;
    }
    while (archive_read_next_header(a, &ae) == ARCHIVE_OK) {
        const char *pn = archive_entry_pathname(ae);
        const char *slash = pn ? strrchr(pn, '/') : NULL;
        if (slash && strcmp(slash + 1, member) == 0) {
            FILE *w = fopen(dst, "wb");
            if (w) {
                const void *buf;
                size_t sz;
                la_int64_t off;
                while (archive_read_data_block(a, &buf, &sz, &off) ==
                       ARCHIVE_OK &&
                       sz > 0)
                    fwrite(buf, 1, sz, w);
                fclose(w);
            }
            found = 1;
            break;
        }
        archive_read_data_skip(a);
    }
    archive_read_free(a);
    return found ? 0 : -1;
}

/* Descarga el release pinneado a outdir/limine-binary.tar.xz con
 * verificacion sha256 y extrae BOOTX64.EFI + limine-bios.sys a outdir. */
static int limine_fetch_binary(const char *outdir)
{
    struct xbps_handle xhp;
    char tar[PATH_MAX + 32], efi[PATH_MAX + 64];
    int rv;
    memset(&xhp, 0, sizeof(xhp));
    if (cfs_mkdir_p(outdir) != 0)
        return -1;
    snprintf(tar, sizeof(tar), "%s/limine-binary.tar.xz", outdir);
    (void)cfs_remove_all(tar);
    if (xbps_init(&xhp) != 0)
        return -1;
    rv = xbps_fetch_file_dest(&xhp, LIMINE_URL, tar, NULL);
    if (rv < 0)
        return -1;
    if (rv == 0 && !path_exists(tar))
        return -1;
    if (!path_exists(tar))
        return -1;
    /* sha256 con libxbps (post-descarga): corrupto = aborta. */
    if (xbps_file_sha256_check(tar, LIMINE_SHA256) != 1)
        return -1;
    snprintf(efi, sizeof(efi), "%s/BOOTX64.EFI", outdir);
    if (limine_extract_member(tar, "BOOTX64.EFI", efi) != 0)
        return -1;
    {
        char sysf[PATH_MAX + 64];
        snprintf(sysf, sizeof(sysf), "%s/limine-bios.sys", outdir);
        (void)limine_extract_member(tar, "limine-bios.sys", sysf);
    }
    /* El tar ya no hace falta despues de la extraccion. */
    (void)cfs_remove_all(tar);
    return 0;
}

/* BOOTX64.EFI para el ESP: (1) limine paquete en el TARGET, (2) limine
 * del HOST, (3) descarga pinneada. Devuelve 0 si dejo el binario en
 * <sysroot>/usr/share/limine/BOOTX64.EFI (dst_path listo para copiar). */
static int limine_provision_efi(const char *sysroot, char *src_out,
                                size_t cap)
{
    static const char *tgt_names[] = {
        "usr/share/limine/BOOTX64.EFI",
        "usr/share/limine/liminex64.efi",
        "usr/share/limine/bin/BOOTX64.EFI",
    };
    static const char *host_names[] = {
        "/usr/share/limine/BOOTX64.EFI",
        "/usr/share/limine/liminex64.efi",
        "/usr/local/share/limine/BOOTX64.EFI",
        "/usr/local/share/limine/liminex64.efi",
    };
    char tdir[PATH_MAX + 48];
    size_t i;
    if (cfs_join(tdir, sizeof(tdir), sysroot, "usr/share/limine") != 0)
        return -1;
    for (i = 0; i < sizeof(tgt_names) / sizeof(tgt_names[0]); ++i) {
        char cand[PATH_MAX + 64];
        if (cfs_join(cand, sizeof(cand), sysroot, tgt_names[i]) != 0)
            continue;
        if (path_exists(cand)) {
            snprintf(src_out, cap, "%s", cand);
            return 0;
        }
    }
    for (i = 0; i < sizeof(host_names) / sizeof(host_names[0]); ++i) {
        if (path_exists(host_names[i])) {
            snprintf(src_out, cap, "%s", host_names[i]);
            return 0;
        }
    }
    /* Descarga pinneada (host-independiente, requiere red). */
    if (limine_fetch_binary(tdir) != 0)
        return -1;
    snprintf(src_out, cap, "%s/BOOTX64.EFI", tdir);
    if (!path_exists(src_out))
        return -1;
    return 0;
}

static int limine_detect_kernel(const char *sysroot,
                                const sys_bootloader_t *bl,
                                char *kpath, size_t kpath_cap,
                                char *ver, size_t ver_cap,
                                char *initrd, size_t initrd_cap)
{
    char boot[4096], cand[PATH_MAX + 64];
    DIR *d;
    struct dirent *de;
    struct stat st;
    time_t best;
    int have_best = 0;
    char bname[260];

    kpath[0] = '\0';
    ver[0] = '\0';
    initrd[0] = '\0';
    if (cfs_join(boot, sizeof(boot), sysroot, "boot") != 0)
        return -1;
    d = opendir(boot);
    best = 0;
    bname[0] = '\0';
    if (bl && bl->kernel[0]) {
        snprintf(cand, sizeof(cand), "%s%s", sysroot, bl->kernel);
        if (stat(cand, &st) == 0 && S_ISREG(st.st_mode)) {
            const char *base = strrchr(bl->kernel, '/');
            base = base ? base + 1 : bl->kernel;
            snprintf(kpath, kpath_cap, "%s", bl->kernel);
            if (strncmp(base, "vmlinuz-", 8) == 0 && strlen(base) > 8)
                snprintf(ver, ver_cap, "%s", base + 8);
        }
    }
    if (!kpath[0] && d) {
        while ((de = readdir(d)) != NULL) {
            if (strncmp(de->d_name, "vmlinuz-", 8) != 0)
                continue;
            snprintf(cand, sizeof(cand), "%s/%s", boot, de->d_name);
            if (stat(cand, &st) != 0 || !S_ISREG(st.st_mode))
                continue;
            /* musl libera el buffer de readdir en closedir: copia el
             * nombre AHORA (no dejes a best_name stal-ear) */
            if (!have_best || st.st_mtime > best ||
                (st.st_mtime == best &&
                 strcmp(bname, de->d_name) < 0)) {
                best = st.st_mtime;
                snprintf(bname, sizeof(bname), "%s", de->d_name);
                have_best = 1;
            }
        }
        closedir(d);
        d = NULL;
        if (have_best && strlen(bname) > 8) {
            snprintf(kpath, kpath_cap, "/boot/%s", bname);
            snprintf(ver, ver_cap, "%s", bname + 8);
        }
    }
    if (!kpath[0])
        return -1;
    if (ver[0]) {
        snprintf(cand, sizeof(cand), "%s/boot/initramfs-%s.img", sysroot,
                 ver);
        if (stat(cand, &st) == 0) {
            snprintf(initrd, initrd_cap, "/boot/initramfs-%s.img", ver);
            return 0;
        }
    }
    snprintf(cand, sizeof(cand), "%s/boot/initramfs.img", sysroot);
    if (stat(cand, &st) == 0)
        snprintf(initrd, initrd_cap, "/boot/initramfs.img");
    return 0;
}

static int configure_limine(const sys_system_t *sys,
                            const char *sysroot, const char *root_uuid)
{
    char cfg_path[4096], kern[4096], kver[129], initrd[1024],
        buf[16384], esp[4096];
    const sys_bootloader_t *bl = &sys->bootloader;
    int have_esp = 0;
    if (limine_detect_kernel(sysroot, bl, kern, sizeof(kern), kver,
                             sizeof(kver), initrd, sizeof(initrd)) != 0) {
        /* Conserva el comportamiento previo. */
        snprintf(kern, sizeof(kern), "%s",
                 bl->kernel[0] ? bl->kernel : "/boot/vmlinuz");
        snprintf(initrd, sizeof(initrd), "%s",
                 bl->initrd[0] ? bl->initrd : "/boot/initramfs.img");
        kver[0] = '\0';
    }
    /* UEFI: limine solo lee SU volumen de arranque (el ESP): alli van
     * limine.conf Y una copia del kernel/initramfs. El ext4 raiz es
     * invisible para el fichero de config una vez booteado limine. */
    if (bl->is_uefi) {
        char ksrc[PATH_MAX + 64];
        if (cfs_join(esp, sizeof(esp), sysroot, "boot/efi") != 0)
            return -1;
        {
            struct stat st;
            have_esp = (stat(esp, &st) == 0 && S_ISDIR(st.st_mode));
        }
        if (have_esp && kern[0]) {
            snprintf(ksrc, sizeof(ksrc), "%s%s", sysroot, kern);
            {
                char kdst[PATH_MAX + 32];
                struct stat st;
                snprintf(kdst, sizeof(kdst), "%s/vmlinuz", esp);
                if (stat(ksrc, &st) == 0 &&
                    cfs_copy_file(ksrc, kdst) == 0)
                    CLOG_INFO("limine: kernel copiado al ESP (%s)",
                              kern);
                else
                    CLOG_WARNING("limine: no se pudo copiar %s al ESP",
                                 ksrc);
            }
            if (initrd[0]) {
                char idst[PATH_MAX + 64], isrc[PATH_MAX + 64];
                struct stat st;
                snprintf(isrc, sizeof(isrc), "%s%s", sysroot, initrd);
                snprintf(idst, sizeof(idst), "%s/initramfs.img", esp);
                if (stat(isrc, &st) == 0 &&
                    cfs_copy_file(isrc, idst) == 0)
                    CLOG_INFO("limine: initramfs copiado al ESP (%s)",
                              initrd);
                else
                    CLOG_WARNING(
                        "limine: no se pudo copiar %s al ESP", isrc);
            }
        }
    } else {
        esp[0] = '\0';
    }
    /* Configlimine 9+ (ver CONFIG.md): ENTRADAS CON '/', keys
     * path/module_path/cmdline, boot():/ = particion del propio
     * limine.conf. Se escribe UN MISMO contenido en los candidatos
     * relevantes del ESP (EFI/BOOT/limine.conf primero y /limine.conf
     * como fallback) y en /boot/limine/limine.conf para BIOS. */
    snprintf(buf, sizeof(buf),
             "timeout: %u\n\n/%s\n    protocol: linux\n",
             bl->timeout ? bl->timeout : 3,
             bl->label[0] ? bl->label : "NoneOS");
    {
        char cmd[560];
        if (!limc_cmdline(bl, root_uuid, cmd, sizeof(cmd)))
            CLOG_WARNING("limine: sin root UUID en cmdline");
        if (bl->is_uefi && have_esp) {
            /* ESP: kernel/initramfs copiados a la raiz del ESP. */
            snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf),
                     "    path: boot():/vmlinuz\n");
            if (initrd[0])
                snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf),
                         "    module_path: boot():/initramfs.img\n");
            snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf),
                     "    cmdline: %s\n", cmd);
            {
                char conf1[PATH_MAX + 32];
                if (cfs_join(conf1, sizeof(conf1), esp,
                             "EFI/BOOT/limine.conf") != 0)
                    return -1;
                if (cfs_write_atomic(conf1, buf, strlen(buf)) != 0) {
                    CLOG_FATAL("Failed to write limine.conf (ESP/EFI/BOOT)");
                    return -1;
                }
            }
            if (cfs_join(cfg_path, sizeof(cfg_path), esp, "limine.conf") !=
                0)
                return -1;
        } else {
            /* BIOS: kernel/initramfs residuales del root/FS de boteo. */
            snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf),
                     "    path: boot():%s\n", kern);
            if (initrd[0])
                snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf),
                         "    module_path: boot():%s\n", initrd);
            snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf),
                     "    cmdline: %s\n", cmd);
            if (cfs_join(cfg_path, sizeof(cfg_path), sysroot,
                         "boot/limine/limine.conf") != 0)
                return -1;
        }
    }
    if (cfs_write_atomic(cfg_path, buf, strlen(buf)) != 0) {
        CLOG_FATAL("Failed to write limine.conf");
        return -1;
    }
    CLOG_INFO("Limine configured successfully");
    return 0;
}

/* Publico: bridge (post-doraclut) necesita regenerar la config limine
 * cuando el initramfs acaba de crearse (ensure: module_path + copia
 * initramfs/initramfs al ESP). */
int configure_limine_refresh(const sys_system_t *sys, const char *sysroot,
                             const char *root_uuid)
{
    return configure_limine(sys, sysroot, root_uuid);
}

static int configure_systemdboot(const sys_system_t *sys,
                                 const char *sysroot,
                                 const char *root_uuid)
{
    char esp[4096], loader_dir[4096], entries_dir[4096],
        loader_conf[4096], entry[4096], kern[4096], initrd[1024],
        buf[16384];
    const sys_bootloader_t *bl = &sys->bootloader;
    CLOG_INFO("Configuring systemd-boot");
    if (cfs_join(esp, sizeof(esp), sysroot, "boot/efi") != 0)
        return -1;
    if (cfs_join(loader_dir, sizeof(loader_dir), esp, "loader") != 0 ||
        cfs_join(entries_dir, sizeof(entries_dir), loader_dir,
                 "entries") != 0)
        return -1;
    if (cfs_mkdir_p(entries_dir) != 0) {
        CLOG_FATAL("Cannot create systemd-boot entries dir");
        return -1;
    }
    if (cfs_join(loader_conf, sizeof(loader_conf), loader_dir,
                 "loader.conf") != 0)
        return -1;
    snprintf(buf, sizeof(buf), "timeout %u\nconsole-mode max\ndefault noneos.conf\n",
             bl->timeout);
    if (cfs_write_atomic(loader_conf, buf, strlen(buf)) != 0) {
        CLOG_FATAL("Cannot write loader.conf");
        return -1;
    }
    if (!bl->kernel[0])
        snprintf(kern, sizeof(kern), "/boot/vmlinuz");
    else
        snprintf(kern, sizeof(kern), "%s", bl->kernel);
    if (!bl->initrd[0])
        snprintf(initrd, sizeof(initrd), "/boot/initramfs.img");
    else
        snprintf(initrd, sizeof(initrd), "%s", bl->initrd);
    if (cfs_join(entry, sizeof(entry), entries_dir, "noneos.conf") != 0)
        return -1;
    snprintf(buf, sizeof(buf), "title %s\nlinux %s\ninitrd %s\noptions",
             bl->label, kern, initrd);
    if (root_uuid && root_uuid[0])
        snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf),
                 " root=UUID=%s", root_uuid);
    if (bl->cmdline[0])
        snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf),
                 " %s", bl->cmdline);
    snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf), "\n");
    if (cfs_write_atomic(entry, buf, strlen(buf)) != 0) {
        CLOG_FATAL("Cannot write noneos.conf (systemd-boot)");
        return -1;
    }
    return 0;
}

static int configure_refind(const sys_system_t *sys, const char *sysroot,
                            const char *root_uuid)
{
    char cfg_path[4096], kern[4096], initrd[1024], params[4200],
        buf[16384];
    const sys_bootloader_t *bl = &sys->bootloader;
    int have_root = (root_uuid && root_uuid[0]);
    CLOG_INFO("Configuring rEFInd");
    if (cfs_join(cfg_path, sizeof(cfg_path), sysroot,
                 "boot/efi/EFI/BOOT/refind_linux.conf") != 0)
        return -1;
    if (!bl->kernel[0])
        snprintf(kern, sizeof(kern), "/boot/vmlinuz");
    else
        snprintf(kern, sizeof(kern), "%s", bl->kernel);
    if (!bl->initrd[0])
        snprintf(initrd, sizeof(initrd), "/boot/initramfs.img");
    else
        snprintf(initrd, sizeof(initrd), "%s", bl->initrd);
    /* params: root=UUID=.. + cmdline (o rw quiet si nada). */
    if (have_root && bl->cmdline[0])
        cfs_snprintf(params, sizeof(params), "root=UUID=%s %s", root_uuid,
                     bl->cmdline);
    else if (have_root)
        cfs_snprintf(params, sizeof(params), "root=UUID=%s", root_uuid);
    else if (bl->cmdline[0])
        snprintf(params, sizeof(params), "%s", bl->cmdline);
    else
        snprintf(params, sizeof(params), "rw quiet");
    if (cfs_snprintf(buf, sizeof(buf), "\"%s\"    \"%s\"    %s    %s\n",
                     bl->label, params, kern, initrd) != 0) {
        CLOG_WARNING("refind_linux.conf truncated");
        return -1;
    }
    if (cfs_write_atomic(cfg_path, buf, strlen(buf)) != 0) {
        CLOG_FATAL("Cannot write refind_linux.conf");
        return -1;
    }
    CLOG_INFO("rEFInd configured successfully");
    return 0;
}

/* ---------- staging ---------- */

typedef struct {
    int active;
    char stage[4096];
    char upper[4096];
    char sysroot[4096];
} staging_t;

static void sweep_stale_stages(const char *sysroot)
{
    char parent[4096], prefix[64];
    DIR *dp;
    struct dirent *de;
    const char *slash = strrchr(sysroot, '/');
    if (slash && slash != sysroot) {
        size_t n = (size_t)(slash - sysroot);
        memcpy(parent, sysroot, n);
        parent[n] = '\0';
    } else {
        snprintf(parent, sizeof(parent), "/");
    }
    snprintf(prefix, sizeof(prefix), "%s.stage-",
             slash ? slash + 1 : sysroot);
    if (!path_is_dir(parent))
        return;
    dp = opendir(parent);
    if (!dp)
        return;
    while ((de = readdir(dp)) != NULL) {
        char full[4096];
        char *endp = NULL;
        long pid;
        if (strncmp(de->d_name, prefix, strlen(prefix)) != 0)
            continue;
        if (cfs_join(full, sizeof(full), parent, de->d_name) != 0)
            continue;
        errno = 0;
        pid = strtol(de->d_name + strlen(prefix), &endp, 10);
        if (errno != 0 || !endp || *endp != '\0' || pid <= 0 ||
            pid > 4194304)
            continue;
        if (kill((pid_t)pid, 0) == 0)
            continue;
        if (errno != ESRCH)
            continue;
        cfs_remove_all(full);
    }
    closedir(dp);
}

static int stage_sysroot_begin(const char *sysroot, staging_t *st)
{
    char fst[128];
    if (!sysroot || !sysroot[0] || strcmp(sysroot, "/") == 0) {
        CLOG_WARNING("staging refused on / (installing directly)");
        return 0;
    }
    if (cfs_mkdir_p(sysroot) != 0) {
        CLOG_WARNING("cannot create sysroot, installing directly");
        return 0;
    }
    sweep_stale_stages(sysroot);
    /* Solo un overlay rancio se desmonta; jamas un fs real. */
    if (rootman_top_fstype_at(sysroot, fst, sizeof(fst)) == 0 &&
        strcmp(fst, "overlay") == 0) {
        CLOG_WARNING("clearing stale overlay on sysroot");
        rootman_unmount_path(sysroot);
    }
    if (rootman_has_nested_mounts(sysroot)) {
        CLOG_WARNING(
            "nested mounts under sysroot, installing directly");
        return 0;
    }
    {
        char stage[4096], upper[4096], work[4096];
        const char *lowers[1];
        snprintf(stage, sizeof(stage), "%s.stage-%d", sysroot,
                 (int)getpid());
        if (cfs_join(upper, sizeof(upper), stage, "upper") != 0 ||
            cfs_join(work, sizeof(work), stage, "work") != 0)
            return 0;
        lowers[0] = sysroot;
        if (rootman_mount_stack(lowers, 1, upper, work, sysroot) != 0) {
            CLOG_WARNING("staging unavailable, installing directly");
            cfs_remove_all(stage);
            return 0;
        }
        st->active = 1;
        snprintf(st->stage, sizeof(st->stage), "%s", stage);
        snprintf(st->upper, sizeof(st->upper), "%s", upper);
        snprintf(st->sysroot, sizeof(st->sysroot), "%s", sysroot);
    }
    CLOG_INFO("staged sysroot install (overlay)");
    return 1;
}

static int stage_sysroot_end(staging_t *st, int success)
{
    char fst2[128];
    if (!st->active)
        return 0;
    st->active = 0;
    rootman_unmount_path(st->sysroot);
    if (rootman_top_fstype_at(st->sysroot, fst2, sizeof(fst2)) == 0 &&
        strcmp(fst2, "overlay") == 0) {
        CLOG_FATAL("staged sysroot still mounted (not materialized)");
        return -1;
    }
    if (success) {
        if (rootman_materialize_upper(st->upper, st->sysroot) != 0) {
            cfs_remove_all(st->stage);
            CLOG_FATAL("Failed to materialize staged install");
            return -1;
        }
        CLOG_INFO("staged install materialized");
    }
    cfs_remove_all(st->stage);
    return 0;
}

/* ---------- apply ---------- */

int configure_apply(configure_t *cfg, sys_system_t *sys,
                    const char *sysroot)
{
    staging_t st;
    char root_uuid[128] = "";
    int staged;
    if (!sys || !sysroot || !sysroot[0] || strcmp(sysroot, "/") == 0) {
        set_err(cfg, "sysroot empty or /");
        return -1;
    }
    if (sys->ndysks == 0 || sys->nusers == 0 || sys->npackages == 0) {
        set_err(cfg, "dysks, users and packages are mandatory");
        return -1;
    }
    if (apply_dysks_config(sys, sysroot, root_uuid, sizeof(root_uuid)) !=
        0)
        return -1;
    staged = stage_sysroot_begin(sysroot, &st);
    (void)staged;
    /* FHS interno: defaults fijos (no configurables). */
    {
        static const struct {
            const char *p;
            mode_t m;
            int sticky;
        } dirs[] = {
            {"/", 0755, 0},
            {"/bin", 0755, 0},
            {"/boot", 0755, 0},
            {"/dev", 0755, 0},
            {"/etc", 0755, 0},
            {"/home", 0755, 0},
            {"/lib", 0755, 0},
            {"/media", 0755, 0},
            {"/mnt", 0755, 0},
            {"/opt", 0755, 0},
            {"/proc", 0755, 0},
            {"/root", 0755, 0},
            {"/run", 0755, 0},
            {"/sbin", 0755, 0},
            {"/srv", 0755, 0},
            {"/sys", 0755, 0},
            {"/tmp", 0777, 1},
            {"/usr", 0755, 0},
            {"/usr/bin", 0755, 0},
            {"/usr/include", 0755, 0},
            {"/usr/lib", 0755, 0},
            {"/usr/share", 0755, 0},
            {"/usr/share/doc", 0755, 0},
            {"/usr/share/man", 0755, 0},
            {"/usr/share/locale", 0755, 0},
            {"/var", 0755, 0},
            {"/var/cache", 0755, 0},
            {"/var/lib", 0755, 0},
            {"/var/log", 0755, 0},
            {"/var/spool", 0755, 0},
            {"/var/tmp", 0777, 1},
            {"/none", 0755, 0},
            {"/none/store", 0755, 0},
            {"/none/by-name", 0755, 0},
            {"/none/by-backend", 0755, 0},
        };
        size_t di;
        for (di = 0; di < sizeof(dirs) / sizeof(dirs[0]); ++di) {
            char p[4096];
            const char *rel = dirs[di].p;
            if (!rel || !rel[0])
                continue;
            if (rel[0] == '/')
                ++rel;
            if (cfs_join(p, sizeof(p), sysroot, rel) != 0)
                continue;
            if (cfs_mkdir_p(p) != 0)
                continue;
            chmod(p, dirs[di].sticky
                        ? (dirs[di].m | S_ISVTX)
                        : dirs[di].m);
        }
    }
    if (apply_hostname_config(sys, sysroot) != 0)
        goto fail;
    if (apply_env_config(sys, sysroot) != 0)
        goto fail;
    if (apply_users_config(sys, sysroot) != 0)
        goto fail;
    {
        apply_result_t r;
        memset(&r, 0, sizeof(r));
        if (apply_init_config(sys, &r) != 0)
            CLOG_WARNING("init services applied with failures");
        memset(&r, 0, sizeof(r));
        if (apply_services_config(sys, &r) != 0)
            CLOG_WARNING("service configs applied with failures");
        apply_result_free(&r);
    }
    {
        /* Paquetes: resolve Auto + plan dual-libc + install. */
        pkgman_config_t pc;
        pkgman_t *pm;
        libc_plan_t plan;
        size_t i;
        int have_auto = 0;
        for (i = 0; i < sys->npackages; ++i) {
            if (sys->packages[i].backend == SYS_BACKEND_AUTO &&
                !sys->packages[i].pinned) {
                have_auto = 1;
                break;
            }
        }
        if (have_auto) {
            pkgman_config_init(&pc);
            snprintf(pc.rootdir, sizeof(pc.rootdir), "%s", sysroot);
            pc.backend = 2;
            pc.fetch_all_pkgsrc = sys->npackages > 0 ? 1 : 0;
            pm = pkgman_new(&pc, NULL, 0);
            if (!pm)
                goto fail;
            for (i = 0; i < sys->npackages; ++i) {
                int r;
                if (sys->packages[i].backend != SYS_BACKEND_AUTO ||
                    sys->packages[i].pinned)
                    continue;
                r = pkgman_resolve_backend(pm, sys->packages[i].name);
                if (r == 2) {
                    CLOG_FATAL(
                        "auto-resolve found no backend for %s",
                        sys->packages[i].name);
                    pkgman_free(pm);
                    goto fail;
                }
                sys->packages[i].backend =
                    (r == 1) ? SYS_BACKEND_PKGSRC : SYS_BACKEND_XBPS;
            }
            pkgman_free(pm);
        }
        memset(&plan, 0, sizeof(plan));
        if (libc_resolve_plan(sys->packages, sys->npackages,
                              sys->libc, &plan, NULL, 0) != 0) {
            CLOG_FATAL("libc plan failed");
            goto fail;
        }
        pkgman_config_init(&pc);
        snprintf(pc.rootdir, sizeof(pc.rootdir), "%s", sysroot);
        pc.backend = 2;
        pc.fetch_all_pkgsrc = 0;
        for (i = 0; i < sys->npackages; ++i) {
            if (sys->packages[i].backend == SYS_BACKEND_PKGSRC &&
                sys->packages[i].fetch_all_pkgsrc) {
                pc.fetch_all_pkgsrc = 1;
                break;
            }
        }
        pm = pkgman_new(&pc, NULL, 0);
        if (!pm)
            goto fail;
        if (pkgman_initialize(pm) != 0) {
            CLOG_FATAL("Failed to initialize package manager");
            pkgman_free(pm);
            goto fail;
        }
        if (pkgman_install_plan(pm, &plan, 0) != 0) {
            CLOG_FATAL("Failed to install packages");
            pkgman_free(pm);
            libc_plan_free(&plan);
            goto fail;
        }
        pkgman_free(pm);
        libc_plan_free(&plan);
    }
    /* Base glue (agetty/udev) despues de instalar paquetes. */
    {
        apply_result_t r;
        memset(&r, 0, sizeof(r));
        if (apply_base_services_config(sys, &r) != 0)
            CLOG_WARNING("base glue applied with failures");
        apply_result_free(&r);
    }
    /* Baja el staging ANTES del bootloader. */
    if (stage_sysroot_end(&st, 1) != 0)
        return -1;
    /* Swapfiles fuera del staging. */
    {
        size_t i;
        for (i = 0; i < sys->nswapfiles; ++i) {
            if (swapfile_apply(sysroot, &sys->swapfiles[i], 1) != 0) {
                CLOG_FATAL("Failed to apply swapfiles");
                return -1;
            }
        }
    }
    /* Bootloader. */
    {
        sys_bootloader_t *bl = &sys->bootloader;
        int has_path = bl->install_path[0] != '\0';
        int has_dev = bl->device[0] != '\0';
        if (!has_path && !has_dev) {
            CLOG_WARNING(
                "No bootloader install path or device set, skipping");
        } else {
            if (!bl->is_uefi && !has_dev) {
                CLOG_FATAL("BIOS bootloader needs device");
                return -1;
            }
            /* LUKS params a cmdline + /boot cifrado solo con GRUB. */
            {
                int crypt_boot = 0;
                size_t i;
                for (i = 0; i < sys->ndysks; ++i) {
                    const sys_dysk_t *d = &sys->dysks[i];
                    char mn[256], pb[2048], ver[64];
                    if (!d->crypt_uuid[0])
                        continue;
                    if (strcmp(d->mountpoint, "/boot/efi") == 0 ||
                        strncmp(d->mountpoint, "/boot/efi/", 10) ==
                            0) {
                        CLOG_FATAL(
                            "encrypted ESP cannot boot (keep /boot/efi clear)");
                        return -1;
                    }
                    if (strcmp(d->mountpoint, "/boot") == 0 ||
                        strncmp(d->mountpoint, "/boot/", 6) == 0)
                        crypt_boot = 1;
                    if (crypt_kernel_params(d->crypt_uuid, mn,
                                            pb, sizeof(pb)) == 0) {
                        if (!bl->cmdline[0] ||
                            strstr(bl->cmdline, pb) == NULL) {
                            if (bl->cmdline[0])
                                strncat(bl->cmdline, " ",
                                         sizeof(bl->cmdline) -
                                             strlen(bl->cmdline) - 1);
                            strncat(bl->cmdline, pb,
                                    sizeof(bl->cmdline) -
                                        strlen(bl->cmdline) - 1);
                        }
                    }
                    if (crypt_boot) {
                        crypt_blkid_version(d->device, ver,
                                            sizeof(ver));
                        if (strcmp(ver, "2") == 0)
                            CLOG_WARNING(
                                "LUKS2 /boot needs grub luks2 module");
                    }
                }
                if (crypt_boot && bl->type != SYS_BL_GRUB) {
                    CLOG_FATAL("encrypted /boot needs GRUB (cryptodisk)");
                    return -1;
                }
                if (crypt_boot)
                    CLOG_INFO("encrypted /boot: GRUB cryptodisk modules on");
            }
            switch ((int)bl->type) {
            case (int)SYS_BL_GRUB: {
                /* install_grub: grub-install + configure_grub. */
                char boot_dir[4096], esp_dir[4096], boot_arg[4352],
                    efi_arg[4352], mod_arg[256];
                const char *modp = NULL;
                int crypt_boot = 0;
                size_t i;
                if (cfs_join(boot_dir, sizeof(boot_dir), sysroot,
                             "boot") != 0)
                    return -1;
                if (cfs_join(esp_dir, sizeof(esp_dir), sysroot,
                             "boot/efi") != 0)
                    return -1;
                snprintf(boot_arg, sizeof(boot_arg),
                         "--boot-directory=%s", boot_dir);
                snprintf(efi_arg, sizeof(efi_arg),
                         "--efi-directory=%s", esp_dir);
                for (i = 0; i < sys->ndysks; ++i) {
                    const sys_dysk_t *d = &sys->dysks[i];
                    if (!d->crypt_uuid[0])
                        continue;
                    if (strcmp(d->mountpoint, "/boot") == 0 ||
                        strncmp(d->mountpoint, "/boot/", 6) == 0)
                        crypt_boot = 1;
                }
                if (crypt_boot) {
                    snprintf(mod_arg, sizeof(mod_arg),
                             "--modules=luks,cryptodisk,luks2");
                    modp = mod_arg;
                }
                if (bl->is_uefi) {
                    char *argv[8];
                    int n = 0;
                    CLOG_INFO("Installing GRUB (UEFI) boot=%s esp=%s",
                              boot_dir, esp_dir);
                    argv[n++] = (char *)"grub-install";
                    argv[n++] = efi_arg;
                    argv[n++] = boot_arg;
                    if (modp)
                        argv[n++] = (char *)modp;
                    argv[n] = NULL;
                    if (cfs_run_wait(argv) != 0) {
                        CLOG_FATAL("grub-install failed");
                        return -1;
                    }
                } else {
                    char *argv[8];
                    int n = 0;
                    CLOG_INFO("Installing GRUB (BIOS) on %s",
                              bl->device);
                    CLOG_INFO(
                        "Make sure you used the device and not the partition");
                    argv[n++] = (char *)"grub-install";
                    argv[n++] = boot_arg;
                    if (modp)
                        argv[n++] = (char *)modp;
                    argv[n++] = (char *)bl->device;
                    argv[n] = NULL;
                    if (cfs_run_wait(argv) != 0) {
                        CLOG_FATAL("grub-install failed");
                        return -1;
                    }
                }
                if (configure_grub(sys, sysroot, root_uuid) != 0)
                    return -1;
                CLOG_INFO("Bootloader installed successfully");
                break;
            }
            case (int)SYS_BL_LIMINE: {
                char efi_src[PATH_MAX + 64], efi_dst[PATH_MAX + 64];
                if (cfs_join(efi_src, sizeof(efi_src), sysroot,
                             "usr/share/limine") != 0)
                    return -1;
                if (cfs_join(efi_dst, sizeof(efi_dst), sysroot,
                             "boot/efi/EFI/BOOT/BOOTX64.EFI") != 0)
                    return -1;
                {
                    const char *slash = strrchr(efi_dst, '/');
                    if (slash && slash != efi_dst) {
                        char parent[PATH_MAX + 32];
                        size_t n = (size_t)(slash - efi_dst);
                        memcpy(parent, efi_dst, n);
                        parent[n] = '\0';
                        if (cfs_mkdir_p(parent) != 0)
                            return -1;
                    }
                }
                if (bl->is_uefi) {
                    char src[PATH_MAX + 64];
                    if (limine_provision_efi(sysroot, src,
                                             sizeof(src)) != 0) {
                        CLOG_FATAL(
                            "limine: BOOTX64.EFI no provisto (target "
                            "sin pkg, host sin limine, y descarga "
                            "fallida)");
                        return -1;
                    }
                    if (cfs_copy_file(src, efi_dst) != 0) {
                        CLOG_FATAL("limine: no se pudo copiar %s al ESP",
                                   src);
                        return -1;
                    }
                    CLOG_INFO("limine: BOOTX64.EFI instalado en el ESP "
                              "(%s)",
                              src[0] == '/' &&
                                      strncmp(src, sysroot,
                                              strlen(sysroot)) == 0
                                  ? "target" : "host/descarga");
                } else {
                    char *argv[4];
                    char bios_src[PATH_MAX + 64], bios_dst[PATH_MAX + 64];
                    if (!bl->device[0]) {
                        CLOG_FATAL("limine BIOS install needs device");
                        return -1;
                    }
                    /* limine-bios.sys debe ser leible por stage2: al FS
                     * de /boot del target (o via host para bios-install). */
                    {
                        char sdir[PATH_MAX + 48];
                        if (cfs_join(sdir, sizeof(sdir), sysroot,
                                     "usr/share/limine") != 0)
                            return -1;
                        if (cfs_join(bios_src, sizeof(bios_src), sysroot,
                                     "usr/share/limine/"
                                     "limine-bios.sys") == 0 &&
                            !path_exists(bios_src)) {
                            if (path_exists(
                                    "/usr/share/limine/limine-bios.sys"))
                                snprintf(bios_src, sizeof(bios_src),
                                         "%s",
                                         "/usr/share/limine/"
                                         "limine-bios.sys");
                            else if (limine_fetch_binary(sdir) == 0)
                                snprintf(bios_src, sizeof(bios_src),
                                         "%s/limine-bios.sys", sdir);
                        }
                        if (path_exists(bios_src)) {
                            if (cfs_join(bios_dst, sizeof(bios_dst),
                                         sysroot, "boot/limine-bios.sys") !=
                                0)
                                return -1;
                            (void)cfs_copy_file(bios_src, bios_dst);
                        }
                    }
                    CLOG_INFO("Installing limine (BIOS) on %s",
                              bl->device);
                    argv[0] = (char *)"limine";
                    argv[1] = (char *)"bios-install";
                    argv[2] = (char *)bl->device;
                    argv[3] = NULL;
                    if (cfs_run_wait(argv) != 0) {
                        CLOG_FATAL("limine bios-install failed");
                        return -1;
                    }
                }
                if (configure_limine(sys, sysroot, root_uuid) != 0)
                    return -1;
                break;
            }
            case (int)SYS_BL_SYSTEMDBOOT: {
                if (!bl->is_uefi) {
                    CLOG_FATAL(
                        "systemd-boot only works on UEFI (no silent fallback to limine)");
                    return -1;
                }
                {
                    char esp[4096], arg[4200], *argv[4];
                    if (cfs_join(esp, sizeof(esp), sysroot,
                                 "boot/efi") != 0)
                        return -1;
                    CLOG_INFO("Installing systemd-boot on %s", esp);
                    snprintf(arg, sizeof(arg), "--esp-path=%s", esp);
                    argv[0] = (char *)"bootctl";
                    argv[1] = arg;
                    argv[2] = (char *)"install";
                    argv[3] = NULL;
                    if (cfs_run_wait(argv) != 0) {
                        CLOG_FATAL("bootctl install failed");
                        return -1;
                    }
                }
                if (configure_systemdboot(sys, sysroot, root_uuid) != 0)
                    return -1;
                break;
            }
            case (int)SYS_BL_REFIND: {
                if (bl->is_uefi) {
                    char *argv[4];
                    CLOG_INFO("Installing rEFInd (UEFI)");
                    argv[0] = (char *)"refind-install";
                    argv[1] = (char *)"--root";
                    argv[2] = (char *)sysroot;
                    argv[3] = NULL;
                    if (cfs_run_wait(argv) != 0) {
                        CLOG_WARNING(
                            "refind-install failed, trying manual copy");
                        /* Manual copy best-effort. */
                        {
                            static const char *candidates[] = {
                                "/usr/share/refind/refind_x64.efi",
                                "/usr/share/refind/BOOTX64.EFI",
                                "/usr/local/share/refind/refind_x64.efi",
                                "/usr/local/share/refind/BOOTX64.EFI",
                            };
                            char esp_efi[4096], dst[4096];
                            int copied = 0;
                            size_t i;
                            if (cfs_join(esp_efi, sizeof(esp_efi),
                                         sysroot,
                                         "boot/efi/EFI") != 0)
                                return -1;
                            if (cfs_mkdir_p(esp_efi) != 0)
                                return -1;
                            for (i = 0;
                                 i < sizeof(candidates) /
                                     sizeof(candidates[0]);
                                 ++i) {
                                if (!path_exists(candidates[i]))
                                    continue;
                                if (cfs_join(dst, sizeof(dst), sysroot,
                                             "boot/efi/EFI/BOOT/BOOTX64.EFI") != 0)
                                    continue;
                                if (cfs_copy_file(candidates[i],
                                                  dst) == 0) {
                                    copied = 1;
                                    CLOG_INFO(
                                        "Manually copied rEFInd from %s",
                                        candidates[i]);
                                    break;
                                }
                            }
                            if (!copied)
                                CLOG_FATAL(
                                    "rEFInd install failed: no EFI binary found");
                        }
                    } else {
                        CLOG_INFO("rEFInd installed via refind-install");
                    }
                } else {
                    char *argv[6];
                    if (!bl->device[0]) {
                        CLOG_FATAL("rEFInd BIOS install requires device");
                        return -1;
                    }
                    CLOG_INFO("Installing rEFInd (BIOS) on %s",
                              bl->device);
                    argv[0] = (char *)"refind-install";
                    argv[1] = (char *)"--usedefault";
                    argv[2] = (char *)bl->device;
                    argv[3] = (char *)"--root";
                    argv[4] = (char *)sysroot;
                    argv[5] = NULL;
                    if (cfs_run_wait(argv) != 0) {
                        CLOG_FATAL("refind-install BIOS failed");
                        return -1;
                    }
                }
                if (configure_refind(sys, sysroot, root_uuid) != 0)
                    return -1;
                break;
            }
            default:
                break;
            }
        }
    }
    /* Secure Boot. */
    if (sys->bootloader.secure_boot.enabled) {
        char keys[4096], khost[4096], esp[4096];
        char efis[64][4096];
        int n, i;
        if (!sys->bootloader.is_uefi) {
            CLOG_FATAL("Secure Boot needs UEFI");
            return -1;
        }
        if (sys->bootloader.secure_boot.keys_dir[0])
            snprintf(keys, sizeof(keys), "%s",
                     sys->bootloader.secure_boot.keys_dir);
        else if (cfs_join(keys, sizeof(keys), sysroot, "none/keys") != 0)
            return -1;
        if (sb_ensure_keys(keys) != 0) {
            CLOG_FATAL("Secure Boot key setup failed");
            return -1;
        }
        if (!sb_tools_available()) {
            CLOG_FATAL(
                "Secure Boot needs sbsigntool (sbsign+sbverify)");
            return -1;
        }
        if (!sys->bootloader.kernel[0])
            snprintf(khost, sizeof(khost), "%s", "/boot/vmlinuz");
        else if (cfs_join(khost, sizeof(khost), sysroot,
                          sys->bootloader.kernel) != 0)
            return -1;
        if (sb_sign_file(keys, khost, "") != 0) {
            CLOG_FATAL("kernel signing failed");
            return -1;
        }
        if (cfs_join(esp, sizeof(esp), sysroot, "boot/efi") != 0)
            return -1;
        n = sb_find_esp_binaries(esp, efis, 64);
        if (n <= 0) {
            CLOG_FATAL("no EFI binaries found to sign");
            return -1;
        }
        for (i = 0; i < n; ++i) {
            if (sb_sign_file(keys, efis[i], "") != 0) {
                CLOG_FATAL("EFI signing failed");
                return -1;
            }
        }
        if (sys->bootloader.secure_boot.shim[0]) {
            char bootdir[4096];
            if (cfs_join(bootdir, sizeof(bootdir), esp, "EFI/BOOT") != 0)
                return -1;
            if (cfs_mkdir_p(bootdir) != 0)
                return -1;
            if (cfs_copy_file(sys->bootloader.secure_boot.shim,
                              bootdir) != 0) {
                /* dst = bootdir/BOOTX64.EFI */
                char dst[4096];
                if (cfs_join(dst, sizeof(dst), bootdir,
                             "BOOTX64.EFI") != 0)
                    return -1;
                if (cfs_copy_file(sys->bootloader.secure_boot.shim,
                                  dst) != 0) {
                    CLOG_FATAL("cannot install shim");
                    return -1;
                }
            }
            CLOG_INFO("Secure Boot artifacts signed; enroll db.cer "
                      "(mokutil --import) or your own firmware DB");
        } else {
            CLOG_INFO("Secure Boot artifacts signed; enroll db.cer "
                      "(mokutil --import) or your own firmware DB");
        }
    }
    return 0;
fail:
    stage_sysroot_end(&st, 0);
    return -1;
}

/* Puente a Configure C++ (ver sysconv.cpp). */
int noneos_apply_c(const sys_system_t *c, const char *sysroot);

int configure_apply_cxx(const sys_system_t *sys, const char *sysroot)
{
    return noneos_apply_c(sys, sysroot);
}
