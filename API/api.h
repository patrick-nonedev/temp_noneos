/* API de configuracion en C puro (migracion C23). Espejo fiel de
 * API/API.hpp: mismos defaults y semantica (dedup keep-first, etc.).
 * Los strings son buffers fijos (ver *_MAX); lo dinamico son los arrays
 * (realloc). Convencion: 0 ok, -1 error. Liberar con sys_system_free()
 * (libera miembros dinamicos; el System en si lo posee el llamante). */
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#include "Services/Swapfile/swapfile.h"

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
