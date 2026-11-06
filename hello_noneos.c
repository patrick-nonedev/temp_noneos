/* hello_noneos.c — ejemplo minimo de uso del SDK NoneOS en la VM.
 *
 * Solo necesita noneos.h + el .a correspondiente (variedades "full":
 * NO llevan -l externas). El .a SIEMPRE va DESPUES del .c/.o:
 *
 *   glibc : cc -std=c23 hello_noneos.c libnone-full-glibc.a -o app
 *   musl  : musl-gcc -static -fno-link-libatomic -std=c23 \
 *                    hello_noneos.c libnone-full-musl.a -o app
 *
 * Salida esperada: "hello noneos: sysroots=/tmp/x (2)".
 */
#include <stdio.h>
#include "noneos.h"

int main(void)
{
    sys_system_t *sys = sys_system_new();

    sys_system_sysroot(sys, "/tmp/x");
    sys_system_init_system(sys, SYS_INIT_RUNIT);
    printf("hello noneos: sysroots=%s (%d)\n", sys->sysroot,
           (int)SYS_INIT_RUNIT);
    sys_system_free(sys);
    return 0;
}
