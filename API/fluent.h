/* API fluida en C (migracion C23). Servicios con prefijo S_ y metodos
 * embebidos (por valor, para encadenar con punto):
 *   sys_service_t *ssh = S_SSH();
 *   ssh->port(ssh, 1234).user(ssh, neko).policy(ssh, SYS_POLICY_NEVER);
 *   sys_system_take_service(sys, ssh); */
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "API/api.h"

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
