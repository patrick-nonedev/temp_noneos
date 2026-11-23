#!/usr/bin/env bash
# sync_files.sh — Sincroniza binarios y headers desde NoneOS/build/
# Uso: ./sync_files.sh
set -euo pipefail

NONEOS_ROOT="/mnt/Datos/code/c++/None/NoneOS"
TEMP_ROOT="/mnt/Datos/code/c++/None/temp_noneos"

echo "==> Sincronizando archivos desde NoneOS/build/..."

# Binarios principales
cp -v "$NONEOS_ROOT/build/nshell" "$TEMP_ROOT/nshell"
cp -v "$NONEOS_ROOT/build/nshell-musl" "$TEMP_ROOT/nshell-musl" 2>/dev/null || true
cp -v "$NONEOS_ROOT/build/noneos-config" "$TEMP_ROOT/noneos-config"
cp -v "$NONEOS_ROOT/build/miniinit" "$TEMP_ROOT/miniinit" 2>/dev/null || true

# Librerías
cp -v "$NONEOS_ROOT/build/libnone.a" "$TEMP_ROOT/libnone.a"
cp -v "$NONEOS_ROOT/build/libnone-musl.a" "$TEMP_ROOT/libnone-musl.a"
cp -v "$NONEOS_ROOT/build/libnone-full-glibc.a" "$TEMP_ROOT/libnone-full-glibc.a"
cp -v "$NONEOS_ROOT/build/libnone-full-musl.a" "$TEMP_ROOT/libnone-full-musl.a"

# Headers
cp -v "$NONEOS_ROOT/build/noneos.h" "$TEMP_ROOT/noneos.h"

# Fuentes de glue (para verificar los fixes)
cp -v "$NONEOS_ROOT/src/Services/glue.c" "$TEMP_ROOT/glue.c"
cp -v "$NONEOS_ROOT/src/Services/glue.h" "$TEMP_ROOT/glue.h"
cp -v "$NONEOS_ROOT/src/Services/glue/mini-init.c" "$TEMP_ROOT/mini-init.c"
cp -v "$NONEOS_ROOT/src/configure.c" "$TEMP_ROOT/configure.c"

# Demos
cp -v "$NONEOS_ROOT/demo/config_demo.c" "$TEMP_ROOT/config_demo.c" 2>/dev/null || true
cp -v "$NONEOS_ROOT/demo/minimal_demo.c" "$TEMP_ROOT/minimal_demo.c" 2>/dev/null || true
cp -v "$NONEOS_ROOT/demo/install_demo.c" "$TEMP_ROOT/install_demo.c" 2>/dev/null || true

echo "==> Sincronización completada."
echo "==> Archivos en $TEMP_ROOT:"
ls -lh "$TEMP_ROOT"/*.a "$TEMP_ROOT"/nshell "$TEMP_ROOT"/noneos-config 2>/dev/null
