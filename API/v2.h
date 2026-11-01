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
#pragma once

#include <stddef.h>

#include "API/api.h"

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
