/* noneos.h — cabecera INDEPENDIENTE de NoneOS (UI en C).
 * Generada por tools/gen-noneos-standalone.sh; no editar a mano.
 * La implementacion vive en libnone.a (glibc) o libnone-full-*.a
 * (con las dependencias fusionadas). Uso en la VM:
 *   cc -std=c23 -I. app.c libnone-full-musl.a -o app
 */
#ifndef NONEOS_STANDALONE_H
#define NONEOS_STANDALONE_H


/* ---- generado desde src/Services/Swapfile/swapfile.h ---- */
#ifdef __cplusplus
extern "C" {
#endif
/* Swapfiles en C puro (migracion C23). Crea ficheros con bloques reales,
 * les pasa mkswap y añade lineas a etc/fstab. Idempotente: existente con
 * magia valida se reutiliza; fichero sin magia = error (no se pisa).
 * 0 ok (o nada que hacer), -1 error limpio. */

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *path;     /* ruta absoluta destino, p.ej. "/swapfile" */
    long size_mb;         /* >0 */
    const char *options;  /* NULL o "" = defaults */
} swapfile_spec_t;

int swapfile_apply(const char *sysroot, const swapfile_spec_t *specs,
                   size_t n);

#ifdef __cplusplus
} /* extern "C" */
#endif
#ifdef __cplusplus
} /* extern "C" */
#endif

/* ---- generado desde src/API/api.h ---- */
#ifdef __cplusplus
extern "C" {
#endif
/* API de configuracion en C puro (migracion C23). Espejo fiel de
 * API/API.hpp: mismos defaults y semantica (dedup keep-first, etc.).
 * Los strings son buffers fijos (ver *_MAX); lo dinamico son los arrays
 * (realloc). Convencion: 0 ok, -1 error. Liberar con sys_system_free()
 * (libera miembros dinamicos; el System en si lo posee el llamante). */

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>


#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SYS_BACKEND_XBPS = 0,
    SYS_BACKEND_PKGSRC = 1,
    SYS_BACKEND_AUTO = 2
} sys_backend_t;

typedef enum {
    SYS_FS_TRADITIONAL = 0,
    SYS_FS_STORE = 1,
    SYS_FS_AUTOMATIC = 2
} sys_fs_type_t;

typedef enum {
    SYS_INIT_SYSTEMD = 0,
    SYS_INIT_DINIT = 1,
    SYS_INIT_RUNIT = 2,
    SYS_INIT_OPENRC = 3,
    /* Sin soporte real: cae a fallback systemd al aplicar. */
    SYS_INIT_S6 = 4
} sys_init_t;

typedef enum {
    SYS_BL_GRUB = 0,
    SYS_BL_LIMINE = 1,
    SYS_BL_SYSTEMDBOOT = 2,
    SYS_BL_REFIND = 3
} sys_bl_type_t;

typedef enum {
    SYS_POLICY_NEVER = 0,
    SYS_POLICY_ONFAILURE = 1,
    SYS_POLICY_ALWAYS = 2
} sys_policy_t;

/* Overlay por paquete (aislamiento): si enabled, el paquete se instala en
 * su propia raiz <sysroot>/none/overlays/<name>/ (mismo mecanismo que los
 * prefijos libc: backend con rootdir) y se expone con vistas de symlinks
 * estilo store. name vacio = nombre del paquete. La activacion la ejecuta
 * el instalador (PackageManager); aqui solo se declara y persiste. */
typedef struct {
    int enabled;
    char name[129];
} sys_overlay_t;

typedef struct {
    char name[129];
    sys_backend_t backend;
    char version[129];
    char binary_name[129];
    char localbase[1024];
    char libc[65];
    char repository[2048];
    char target_arch[129];
    char alias[129];
    char compiler[1024];
    char prefix[1024];
    int pinned;
    uint32_t priority;
    sys_fs_type_t fs_type;
    int fetch_all_pkgsrc;
    sys_overlay_t overlay;
} sys_package_t;

void sys_package_init(sys_package_t *p, const char *name);

typedef struct {
    char device[1024];
    char mountpoint[1024];
    char fs[64];
    char type[64];
    int boot;
    int swap;
    char options[1024];
    char crypt_uuid[128];
    char crypt_keyfile[1024];
    char crypt_options[1024];
    int dump;
    int pass;
} sys_dysk_t;

void sys_dysk_init(sys_dysk_t *d, const char *device);

typedef struct {
    char name[65];
    char **groups;
    size_t ngroups;
    size_t groups_cap;
    char shell[256];
    uid_t uid;
} sys_user_t;

void sys_user_init(sys_user_t *u, const char *name);
int sys_user_add_group(sys_user_t *u, const char *group);
void sys_user_free(sys_user_t *u);

typedef enum {
    SYS_SVUSER_STRING = 0,
    SYS_SVUSER_USER = 1
} sys_svuser_kind_t;

typedef struct {
    char name[128];
    char value[1024];
} sys_option_t;

typedef struct sys_service sys_service_t; /* metodos necesitan el tipo */

/* Formas de metodo. Devuelven COPIA por valor (shallow: comparte
 * options/raws/grupos con el original) para encadenar con punto:
 *   s.port(&s, 22).user(&s, neko).policy(&s, SYS_POLICY_NEVER);
 * La copia es PRESTADA: nunca liberarla (solo el original es dueno).
 * En fallo interno devuelven cero (seguir encadenando peta en alto,
 * igual que el NULL de antes). */
typedef sys_service_t (*sys_svc_m0_t)(sys_service_t *);
typedef sys_service_t (*sys_svc_m_policy_t)(sys_service_t *,
                                            sys_policy_t);
typedef sys_service_t (*sys_svc_m_str_t)(sys_service_t *, const char *);
typedef sys_service_t (*sys_svc_m_opt_t)(sys_service_t *, const char *,
                                        const char *);
typedef sys_service_t (*sys_svc_m_opts_t)(sys_service_t *,
                                          const sys_option_t *,
                                          size_t);
typedef sys_service_t (*sys_svc_m_user_t)(sys_service_t *,
                                          const sys_user_t *);
typedef sys_service_t (*sys_svc_m_bool_t)(sys_service_t *, int);
typedef sys_service_t (*sys_svc_m_yesno_t)(sys_service_t *, int);
typedef sys_service_t (*sys_svc_m_uint_t)(sys_service_t *, unsigned);

typedef struct sys_service {
    char name[129];
    int enabled;
    sys_svuser_kind_t user_kind;
    sys_user_t user_obj;
    char user_str[65];
    char run_group[65];
    sys_policy_t restart_policy;
    sys_option_t *options;
    size_t noptions;
    size_t options_cap;
    char **raws;
    size_t nraws;
    size_t raws_cap;
    /* Metodos embebidos: s.port(&s, 22).user(&s, n) encadena con punto
     * (el self va explicito: C no tiene this). Se ligan en
     * sys_service_init; las copias los conservan. */
    sys_svc_m0_t enable;
    sys_svc_m0_t disable;
    sys_svc_m_policy_t policy;
    sys_svc_m_str_t group;
    sys_svc_m_opt_t option;
    sys_svc_m_opts_t opts;
    sys_svc_m_str_t raw;
    sys_svc_m_user_t user;
    sys_svc_m_str_t user_name;
    sys_svc_m_uint_t port;
    sys_svc_m_yesno_t permit_root_login;
    sys_svc_m_bool_t alsa;
    sys_svc_m_bool_t pulse;
    sys_svc_m_bool_t jack;
    sys_svc_m_bool_t bluetooth;
    sys_svc_m_bool_t wifi;
    sys_svc_m_bool_t ethernet;
    sys_svc_m_bool_t network_configuration;
    sys_svc_m_str_t unix_sock_group;
    sys_svc_m_bool_t tcp;
    sys_svc_m_bool_t socket;
    sys_svc_m_bool_t accept_routes;
    sys_svc_m_uint_t worker_processes;
    sys_svc_m_str_t workgroup;
    sys_svc_m_str_t server;
    sys_svc_m_str_t interface;
    sys_svc_m_uint_t gui_port;
    sys_svc_m_bool_t experimental;
    sys_svc_m_bool_t ipv6;
} sys_service_t;

void sys_service_init(sys_service_t *s, const char *name);
int sys_service_add_option(sys_service_t *s, const char *name,
                           const char *value);
int sys_service_add_raw(sys_service_t *s, const char *line);
void sys_service_free(sys_service_t *s);

/* Secure Boot minimo (UEFI): firma kernel + EFI con claves propias
 * (se generan si faltan). keys_dir vacio = <sysroot>/none/keys. shim
 * opcional: prestado y firmado, fallback BOOTX64.EFI (sin el, arranque
 * via DB propia enrolada en firmware). */
typedef struct {
    int enabled;
    char keys_dir[4096];
    char shim[4096];
} sys_secure_boot_t;

typedef struct {
    sys_bl_type_t type;
    char kernel[4096];
    char install_path[4096];
    char device[1024];
    char initrd[1024];
    char cmdline[4096];
    char label[129];
    int default_entry;
    uint32_t timeout;
    int is_uefi;
    sys_secure_boot_t secure_boot;
} sys_bootloader_t;

void sys_bootloader_init(sys_bootloader_t *b, sys_bl_type_t type);

/* Variable de entorno global (va a /etc/environment al aplicar). */
typedef struct {
    char key[128];
    char value[1024];
} sys_env_t;

typedef struct {
    sys_package_t *packages;
    size_t npackages;
    size_t packages_cap;
    sys_dysk_t *dysks;
    size_t ndysks;
    size_t dysks_cap;
    swapfile_spec_t *swapfiles;
    size_t nswapfiles;
    size_t swapfiles_cap;
    char libc[65];
    char hostname[65];
    char sysroot[4096];
    sys_user_t *users;
    size_t nusers;
    size_t users_cap;
    sys_service_t *services;
    size_t nservices;
    size_t services_cap;
    sys_env_t *envs;
    size_t nenvs;
    size_t envs_cap;
    sys_init_t init_system;
    sys_bootloader_t bootloader;
} sys_system_t;

void sys_system_init(sys_system_t *s);
void sys_system_free(sys_system_t *s);

/* 0 ok, -1 error. Duplicado = keep-first silencioso como en C++. */
int sys_add_package(sys_system_t *s, const sys_package_t *p);
int sys_add_dysk(sys_system_t *s, const sys_dysk_t *d);
int sys_add_swapfile(sys_system_t *s, const swapfile_spec_t *sw);
int sys_add_user(sys_system_t *s, const sys_user_t *u);
int sys_add_service(sys_system_t *s, const sys_service_t *svc);

#ifdef __cplusplus
} /* extern "C" */
#endif
#ifdef __cplusplus
} /* extern "C" */
#endif

/* ---- generado desde src/API/fluent.h ---- */
#ifdef __cplusplus
extern "C" {
#endif
/* API fluida en C (migracion C23). Servicios con prefijo S_ y metodos
 * embebidos (por valor, para encadenar con punto):
 *   sys_service_t *ssh = S_SSH();
 *   ssh->port(ssh, 1234).user(ssh, neko).policy(ssh, SYS_POLICY_NEVER);
 *   sys_system_take_service(sys, ssh); */

#include <stddef.h>
#include <stdint.h>


#ifdef __cplusplus
extern "C" {
#endif

/* ---------- system ---------- */

sys_system_t *sys_system_new(void);
/* Solo lo que quieras configurar; devuelve sys. */
sys_system_t *sys_system_hostname(sys_system_t *s, const char *hostname);
sys_system_t *sys_system_libc(sys_system_t *s, const char *libc);
sys_system_t *sys_system_sysroot(sys_system_t *s, const char *sysroot);
sys_system_t *sys_system_init_system(sys_system_t *s, sys_init_t init);
/* Bajar de nivel: bootloader embebido (prestado, NO liberar). */
sys_bootloader_t *sys_system_bootloader(sys_system_t *s);

/* Env global (va a /etc/environment). Misma key = reemplaza (no dup).
 * Key valida: [A-Za-z0-9_]+, valor sin salto de linea. NULL si invalido. */
sys_system_t *sys_system_env(sys_system_t *s, const char *key,
                             const char *value);
sys_system_t *sys_system_envs(sys_system_t *s, const sys_env_t *envs,
                              size_t n);

/* Swap por particion: dysk con flag swap (fstab "none swap").
 * Equivale al .swapfile("/dev/...") de una sola llamada. */
int sys_system_take_swapdev(sys_system_t *s, const char *device);

/* Adoptan (liberan el hijo siempre, tambien en duplicado keep-first).
 * 0 ok, -1 error (hijo NO consumido: sigue del llamante). */
int sys_system_take_package(sys_system_t *s, sys_package_t *p);
int sys_system_take_dysk(sys_system_t *s, sys_dysk_t *d);
int sys_system_take_user(sys_system_t *s, sys_user_t *u);
int sys_system_take_service(sys_system_t *s, sys_service_t *svc);
int sys_system_take_swapfile(sys_system_t *s, swapfile_spec_t *sw);
/* Bulk: arrays de punteros propios (el array es del llamante). */
int sys_system_take_packages(sys_system_t *s, sys_package_t **ps,
                             size_t n);
int sys_system_take_dysks(sys_system_t *s, sys_dysk_t **ds, size_t n);
int sys_system_take_users(sys_system_t *s, sys_user_t **us, size_t n);
int sys_system_take_services(sys_system_t *s, sys_service_t **svcs,
                             size_t n);
int sys_system_take_swapfiles(sys_system_t *s, swapfile_spec_t **sws,
                              size_t n);

/* ---------- package ---------- */

sys_package_t *sys_package_new(const char *name);
void sys_package_free(sys_package_t *p);
sys_package_t *sys_package_version(sys_package_t *p, const char *version);
sys_package_t *sys_package_backend(sys_package_t *p, sys_backend_t backend);
sys_package_t *sys_package_pin(sys_package_t *p);
sys_package_t *sys_package_unpin(sys_package_t *p);
sys_package_t *sys_package_binary_name(sys_package_t *p, const char *name);
sys_package_t *sys_package_localbase(sys_package_t *p, const char *path);
sys_package_t *sys_package_priority(sys_package_t *p, uint32_t priority);
sys_package_t *sys_package_fetch_all(sys_package_t *p);
sys_package_t *sys_package_skip_fetch(sys_package_t *p);
sys_package_t *sys_package_libc(sys_package_t *p, const char *libc);
sys_package_t *sys_package_repository(sys_package_t *p, const char *repo);
sys_package_t *sys_package_target_arch(sys_package_t *p, const char *arch);
sys_package_t *sys_package_alias(sys_package_t *p, const char *alias);
sys_package_t *sys_package_compiler(sys_package_t *p, const char *cc);
sys_package_t *sys_package_prefix(sys_package_t *p, const char *prefix);
/* Overlay aislado (name NULL/vacio = nombre del paquete). No forma parte
 * de la clave dedup (igual que en DB): mismo paquete dos veces colapsa. */
sys_package_t *sys_package_overlay(sys_package_t *p, const char *name);
sys_package_t *sys_package_no_overlay(sys_package_t *p);

/* ---------- dysk ---------- */

sys_dysk_t *sys_dysk_new(const char *device);
void sys_dysk_free(sys_dysk_t *d);
sys_dysk_t *sys_dysk_mountpoint(sys_dysk_t *d, const char *mountpoint);
sys_dysk_t *sys_dysk_fs(sys_dysk_t *d, const char *fs);
sys_dysk_t *sys_dysk_boot(sys_dysk_t *d, int boot);
sys_dysk_t *sys_dysk_swap(sys_dysk_t *d);
sys_dysk_t *sys_dysk_options(sys_dysk_t *d, const char *options);
sys_dysk_t *sys_dysk_type(sys_dysk_t *d, const char *type);
sys_dysk_t *sys_dysk_crypt_uuid(sys_dysk_t *d, const char *uuid);
sys_dysk_t *sys_dysk_crypt_keyfile(sys_dysk_t *d, const char *keyfile);
sys_dysk_t *sys_dysk_crypt_options(sys_dysk_t *d, const char *options);
sys_dysk_t *sys_dysk_dump(sys_dysk_t *d, int dump);
sys_dysk_t *sys_dysk_pass(sys_dysk_t *d, int pass);

/* ---------- swapfile (strings prestadas) ---------- */

swapfile_spec_t *sys_swapfile_new(const char *path);
void sys_swapfile_free(swapfile_spec_t *sw);
swapfile_spec_t *sys_swapfile_size_mb(swapfile_spec_t *sw, long size_mb);
swapfile_spec_t *sys_swapfile_options(swapfile_spec_t *sw,
                                      const char *options);

/* ---------- user ---------- */

sys_user_t *sys_user_new(const char *name);
void sys_user_free_heap(sys_user_t *u);
sys_user_t *sys_user_uid(sys_user_t *u, unsigned uid);
sys_user_t *sys_user_shell(sys_user_t *u, const char *shell);
sys_user_t *sys_user_group(sys_user_t *u, const char *group);
sys_user_t *sys_user_groups(sys_user_t *u, const char *const *groups,
                            size_t n);

/* ---------- servicios (prefijo S_) ----------
 *
 * Los setters son metodos embebidos (ver api.h): mismo orden que el C++,
 * de izquierda a derecha:
 *   sys_service_t *ssh = S_SSH();
 *   ssh->port(ssh, 1234);
 *   ssh->user(ssh, neko);
 *   ssh->policy(ssh, SYS_POLICY_NEVER);
 *   sys_system_take_service(sys, ssh);
 * (Anidados dentro-afuera tambien vale.) */

sys_service_t *S_new(const char *name);
void S_free(sys_service_t *s);
/* Bajar de nivel: user_obj embebido (prestado, NO liberar). */
sys_user_t *S_user_obj(sys_service_t *s);

/* Presets (el nombre del servicio ya viene puesto). */
sys_service_t *S_SSH(void);
sys_service_t *S_PIPEWIRE(void);
sys_service_t *S_WIREPLUMBER(void);
sys_service_t *S_NETWORKMANAGER(void);
sys_service_t *S_IWD(void);
sys_service_t *S_LIBVIRT(void);
sys_service_t *S_PODMAN(void);
sys_service_t *S_DOCKER(void);
sys_service_t *S_TAILSCALE(void);
sys_service_t *S_NGINX(void);
sys_service_t *S_CADDY(void);
sys_service_t *S_SAMBA(void);
sys_service_t *S_CHRONY(void);
sys_service_t *S_UNBOUND(void);
sys_service_t *S_DNSMASQ(void);
sys_service_t *S_SYNCTHING(void);
sys_service_t *S_BLUEZ(void);
sys_service_t *S_AVAHI(void);

/* ---------- bootloader (sobre prestado de sys_system_bootloader) ---------- */

sys_bootloader_t *sys_bootloader_type(sys_bootloader_t *b,
                                      sys_bl_type_t type);
sys_bootloader_t *sys_bootloader_kernel(sys_bootloader_t *b,
                                       const char *path);
sys_bootloader_t *sys_bootloader_initrd(sys_bootloader_t *b,
                                       const char *path);
sys_bootloader_t *sys_bootloader_cmdline(sys_bootloader_t *b,
                                        const char *cmdline);
sys_bootloader_t *sys_bootloader_timeout(sys_bootloader_t *b, int timeout);
sys_bootloader_t *sys_bootloader_install_path(sys_bootloader_t *b,
                                              const char *path);
sys_bootloader_t *sys_bootloader_device(sys_bootloader_t *b,
                                       const char *device);
sys_bootloader_t *sys_bootloader_label(sys_bootloader_t *b,
                                      const char *label);
sys_bootloader_t *sys_bootloader_default_entry(sys_bootloader_t *b,
                                               int entry);
sys_bootloader_t *sys_bootloader_uefi(sys_bootloader_t *b);
sys_bootloader_t *sys_bootloader_bios(sys_bootloader_t *b);
/* Bajar de nivel: secure_boot embebido (prestado, NO liberar). */
sys_secure_boot_t *sys_bootloader_secure_boot(sys_bootloader_t *b);
sys_secure_boot_t *sys_secure_boot_enable(sys_secure_boot_t *sb);
sys_secure_boot_t *sys_secure_boot_keys_dir(sys_secure_boot_t *sb,
                                            const char *dir);
sys_secure_boot_t *sys_secure_boot_shim(sys_secure_boot_t *sb,
                                        const char *shim);

#ifdef __cplusplus
} /* extern "C" */
#endif
#ifdef __cplusplus
} /* extern "C" */
#endif

/* ---- generado desde src/API/v2.h ---- */
#ifdef __cplusplus
extern "C" {
#endif
/* API de usuario 1:1 con el sketch (tmp/demo.c). Tipos agregados con
 * strings PRESTADAS (literales; viven hasta apply) + builders por valor:
 *   system_t sys = create_system();
 *   sys = system_libc(sys, LIBC_MUSL);
 *   sys = system_packages(sys, pkgs, NELEMS(pkgs));
 *   apply_system_to(&sys, "/mnt/target");
 * (En C no hay `a.b(c)` sobre valores: los builders toman y devuelven
 * system_t por valor y se reasignan. Dentro-afuera anidado tambien vale.)
 * Colecciones (arrays, opts, env) son PROPIAS (strdup); escalares, no.
 * free_system() libera lo propio. sys.error != NULL = fallo de build
 * (tensionar OOM o dato invalido en bulk); apply tambien falla alto. */

#include <stddef.h>


#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *name;
    const char *groups; /* "wheel audio" (uno o mas espacios) */
    const char *shell;
    const char *uid;  /* NULL/"" = automatico */
    const char *gid;  /* NULL/"" = automatico; con valor = error (sin soporte) */
} user_t;

typedef enum {
    LIBC_FALLBACK = 0,
    LIBC_GLIBC,
    LIBC_MUSL,
    LIBC_BSD,
    LIBC_CUSTOM /* + system_libc_custom() */
} libc_t;

typedef enum {
    M_XBPS = 0,
    M_PKGSRC
    /* M_PKGIT sin soporte: ni compila (a proposito) */
} package_manager_t;

typedef struct {
    package_manager_t manager;
    const char *mirrors; /* "url1 url2" o NULL = fallback */
} provider_t;

typedef struct {
    const char *name; /* ""/NULL = nombre del paquete */
} overlay_t;

typedef struct {
    const char *name;
    libc_t libc;
    provider_t provider;
    const char *version; /* NULL = cualquiera */
    int pinned;          /* (tu sketch: pined) */
    int overlayd;
    overlay_t overlay;
} package_t;
typedef package_t pkg_t; /* tu alias por comodidad */

typedef enum {
    SVC_NEVER = 0,
    SVC_ONFAILURE = 1,
    SVC_ALWAYS = 2
} svc_policy_t;

typedef struct {
    const char *name; /* prestada */
    int enabled;
    const char *user;  /* prestada, "" = default */
    int policy;        /* SVC_* */
    const char *group; /* prestada */
    const char **opt_keys;   /* PROPIAS */
    const char **opt_values; /* PROPIAS */
    size_t nopts, opts_cap;
    const char **raws; /* PROPIAS */
    size_t nraws, raws_cap;
    const char *_err; /* sticky OOM (no tocar) */
} service_t;

typedef struct {
    const char *mountpoint;
    const char *uuid;   /* o name: uno obligatorio (ver abajo) */
    const char *name;   /* /dev/..., "UUID=...", "LABEL=..." */
    const char *filesystem;
    const char *options; /* NULL = defaults */
} dysk_t;

typedef enum {
    INIT_SYSTEMD = 0,
    INIT_RUNIT,
    INIT_DINIT,
    INIT_OPENRC,
    INIT_S6 /* cae a fallback systemd */
} init_system_t;

typedef enum {
    BOOT_GRUB = 0,
    BOOT_LIMINE,
    BOOT_REFIND,
    BOOT_SYSTEMDBOOT
} bootloader_t;

typedef enum {
    BIOS = 0,
    UEFI
} uefi_bios_t;

typedef struct {
    int enabled;
    const char *keys_dir; /* NULL = <sysroot>/none/keys */
    const char *shim;     /* NULL = sin shim */
} secure_boot_t;

typedef struct {
    bootloader_t bootloader;
    const char *kernel;
    const char *ramdisk; /* = initrd */
    int timeout;
    const char *install_path;
    const char *device; /* BIOS: disco entero (ej /dev/vda); UEFI: opcional */
    uefi_bios_t firmware;
    secure_boot_t secure_boot;
} bootloader_cfg_t;

typedef struct {
    provider_t provider;
    int init_system; /* INIT_* */
    const char *hostname; /* NULL = sin fichero hostname */
    bootloader_cfg_t bootloader;
    int libc; /* LIBC_* */
    const char *libc_custom; /* solo LIBC_CUSTOM */
    user_t *users;
    size_t nusers, users_cap;
    service_t *services;
    size_t nservices, services_cap;
    package_t *packages;
    size_t npackages, packages_cap;
    dysk_t *dysks;
    size_t ndysks, dysks_cap;
    int has_crypt;
    const char *crypt_uuid;
    const char *crypt_keyfile; /* NULL = passphrase en boot */
    const char *crypt_opts;    /* NULL = defaults */
    const char *swapfile;      /* /dev/... (particion) */
    const char **env_keys;     /* PROPIAS */
    const char **env_values;   /* PROPIAS */
    size_t nenvs, envs_cap;
    const char *error; /* sticky (no tocar): ultimo fallo de builder */
} system_t;

/* ---------- sistema ---------- */

system_t create_system(void);
void free_system(system_t *s);
system_t system_provider(system_t s, provider_t p);
system_t system_init(system_t s, int init);
system_t system_hostname(system_t s, const char *hostname);
system_t system_bootloader(system_t s, bootloader_cfg_t b);
system_t system_libc(system_t s, int libc);
system_t system_libc_custom(system_t s, const char *name);
system_t system_users(system_t s, const user_t *us, size_t n);
/* Transfiere propiedad de opts/raws (deja los originales vacios). */
system_t system_services(system_t s, const service_t *svcs, size_t n);
system_t system_packages(system_t s, const package_t *ps, size_t n);
system_t system_dysks(system_t s, const dysk_t *ds, size_t n);
system_t system_crypt(system_t s, const char *uuid, const char *keyfile,
                      const char *opts);
system_t system_swapfile(system_t s, const char *device);
system_t system_env(system_t s, const char *key, const char *value);

/* Construye sys_system_t (heap, el llamante libera con sys_system_free +
 * free). NULL + err si el sistema es invalido (ver reglas en v2.c). */
sys_system_t *system_build(const system_t *s, char *err, size_t errcap);
/* Build + Configure sobre sysroot (sin fallback peligroso: vacio = -1).
 * 0 ok, -1 error. */
int apply_system_to(const system_t *s, const char *sysroot);

/* ---------- servicios: presets (por valor) + withers ---------- */

service_t svc_enable(service_t s);
service_t svc_disable(service_t s);
service_t svc_policy(service_t s, int policy);
service_t svc_user(service_t s, const char *user);
service_t svc_group(service_t s, const char *group);
service_t svc_option(service_t s, const char *key, const char *value);
service_t svc_raw(service_t s, const char *line);
void free_service(service_t *s);

service_t svc_ssh(void);
service_t svc_port(service_t s, unsigned port);
service_t svc_permit_root_login(service_t s, int allow);
service_t svc_pipewire(void);
service_t svc_alsa(service_t s, int on);
service_t svc_pulse(service_t s, int on);
service_t svc_jack(service_t s, int on);
service_t svc_wireplumber(void);
service_t svc_bluetooth(service_t s, int on);
service_t svc_networkmanager(void);
service_t svc_wifi(service_t s, int on);
service_t svc_ethernet(service_t s, int on);
service_t svc_iwd(void);
service_t svc_network_configuration(service_t s, int on);
service_t svc_libvirt(void);
service_t svc_unix_sock_group(service_t s, const char *group);
service_t svc_tcp(service_t s, int on);
service_t svc_podman(void);
service_t svc_socket(service_t s, int on);
service_t svc_docker(void);
service_t svc_tailscale(void);
service_t svc_accept_routes(service_t s, int on);
service_t svc_nginx(void);
service_t svc_worker_processes(service_t s, unsigned n);
service_t svc_caddy(void);
service_t svc_samba(void);
service_t svc_workgroup(service_t s, const char *wg);
service_t svc_chrony(void);
service_t svc_server(service_t s, const char *server);
service_t svc_unbound(void);
service_t svc_dnsmasq(void);
service_t svc_interface(service_t s, const char *iface);
service_t svc_syncthing(void);
service_t svc_gui_port(service_t s, unsigned port);
service_t svc_bluez(void);
service_t svc_experimental(service_t s, int on);
service_t svc_avahi(void);
service_t svc_ipv6(service_t s, int on);

#ifdef __cplusplus
} /* extern "C" */
#endif
#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* NONEOS_STANDALONE_H */
