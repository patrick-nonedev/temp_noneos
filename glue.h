/* Glue de sistema base (Services). agetty/udev/evdev por init system:
 * systemd, runit, dinit y openrc. Ficheros idempotentes y best-effort
 * (solo crea lo que falta; nunca sobrescribe lo del paquete base).
 * Ver src/Services/glue.c. Convencion: 0 ejecutado (mirar el
 * apply_result_t), -1 args. */
#pragma once

#include "Services/apply.h"

#ifdef __cplusplus
extern "C" {
#endif

/* getty (tty1..tty6), udevd (svc + coldplug) y modulos de input
 * (evdev/uinput) segun sys->init_system y sys->sysroot. */
int apply_base_services_config(const sys_system_t *sys, apply_result_t *res);

/* BOOT GLUE: todo lo que hace el sistema ARRANCABLE, propiedad del
 * glue (kernel detect + depmod + initramfs). Escribe en kver/kinitrd
 * lo detectado/generado (initrd vacio = fallo: acosar CLOG). El build
 * del initramfs: dracut (chroot binds) -> mkinitfs/mkinitcpio ->
 * initramfs embebido propio (mini-init estatico + modulos del target
 * descomprimidos + packer cpio en C). Devuelve 0 con initrd listo,
 * -1 si no hay kernel, 1 si no logro generarlo. */
int apply_boot_glue(const sys_system_t *sys, char *kver, size_t kvercap,
                    char *initrd, size_t icap);

#ifdef __cplusplus
} /* extern "C" */
#endif
