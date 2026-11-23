#!/usr/bin/env bash
# test_all.sh — Flujo completo de testeo automatizado
#
# Pasos:
#   1. Build de NoneOS (librerías + binarios)
#   2. Tests unitarios (make check)
#   3. Sync de binarios a temp_noneos
#   4. Push al repo remoto (opcional)
#   5. Crear disco QEMU y arrancar VM
#
# Uso:
#   ./test_all.sh              # Todo el flujo hasta tests
#   ./test_all.sh --push       # Incluye push al repo
#   ./test_all.sh --qemu       # Todo + arrancar QEMU
#   ./test_all.sh --full       # Todo + push + QEMU
#
set -euo pipefail

NONEOS_ROOT="/mnt/Datos/code/c++/None/NoneOS"
TEMP_ROOT="/mnt/Datos/code/c++/None/temp_noneos"

# Colores
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

log_step() {
    echo -e "${BLUE}============================================================${NC}"
    echo -e "${BLUE}  $1${NC}"
    echo -e "${BLUE}============================================================${NC}"
}

log_ok() {
    echo -e "${GREEN}[OK]${NC} $1"
}

log_fail() {
    echo -e "${RED}[FAIL]${NC} $1"
}

log_warn() {
    echo -e "${YELLOW}[WARN]${NC} $1"
}

# ============================================================
# Paso 1: Build de NoneOS
# ============================================================
step_build() {
    log_step "Paso 1: Build de NoneOS"
    cd "$NONEOS_ROOT"

    echo "==> Compilando librerías y binarios..."
    if make -j 2>&1 | tail -5; then
        log_ok "Build completado"
    else
        log_fail "Build falló"
        return 1
    fi

    echo "==> Verificando artefactos..."
    local artifacts=("build/nshell" "build/noneos-config" "build/libnone.a" "build/libnone-full-glibc.a" "build/noneos.h")
    for a in "${artifacts[@]}"; do
        if [[ -f "$NONEOS_ROOT/$a" ]]; then
            log_ok "$a"
        else
            log_fail "$a no encontrado"
        fi
    done
}

# ============================================================
# Paso 2: Tests unitarios
# ============================================================
step_tests() {
    log_step "Paso 2: Tests unitarios (make check)"
    cd "$NONEOS_ROOT"

    echo "==> Ejecutando make check..."
    local output
    if output=$(make check 2>&1); then
        echo "$output" | tail -30
        # Contar resultados
        local ok fail skip
        ok=$(echo "$output" | grep -c "OK" || true)
        fail=$(echo "$output" | grep -c "FAIL" || true)
        skip=$(echo "$output" | grep -c "SKIP" || true)
        log_ok "Tests: ${ok} OK, ${fail} FAIL, ${skip} SKIP"
        if [[ "$fail" -gt 0 ]]; then
            log_fail "Hay tests fallando"
            return 1
        fi
    else
        echo "$output" | tail -30
        log_fail "make check falló"
        return 1
    fi
}

# ============================================================
# Paso 3: Sync de binarios
# ============================================================
step_sync() {
    log_step "Paso 3: Sync de binarios a temp_noneos"
    cd "$TEMP_ROOT"
    if ./sync_files.sh; then
        log_ok "Sync completado"
    else
        log_fail "Sync falló"
        return 1
    fi
}

# ============================================================
# Paso 4: Push (opcional)
# ============================================================
step_push() {
    log_step "Paso 4: Push al repo remoto"
    cd "$TEMP_ROOT"
    local msg="${1:-Auto-sync: glue fixes + binaries $(date +%Y-%m-%d)}"
    if ./push.sh "$msg"; then
        log_ok "Push completado"
    else
        log_warn "Push falló (posiblemente sin cambios o sin conexión)"
    fi
}

# ============================================================
# Paso 5: QEMU (opcional)
# ============================================================
step_qemu() {
    log_step "Paso 5: Arrancar QEMU para testeo"
    cd "$TEMP_ROOT"

    echo ""
    echo -e "${YELLOW}Nota: QEMU arrancará en modo interactivo.${NC}"
    echo -e "${YELLOW}Para boot directo después de instalar: ./run_qemu.sh --boot${NC}"
    echo ""

    if ./run_qemu.sh; then
        log_ok "QEMU ejecutado"
    else
        log_fail "QEMU falló"
        return 1
    fi
}

# ============================================================
# Main
# ============================================================
main() {
    local mode="${1:-}"

    echo -e "${GREEN}NoneOS Test Pipeline${NC}"
    echo -e "Fecha: $(date)"
    echo ""

    # Pasos obligatorios
    step_build || exit 1
    step_tests || exit 1
    step_sync || exit 1

    # Pasos opcionales
    case "$mode" in
        --push)
            step_push
            ;;
        --qemu)
            step_qemu
            ;;
        --full)
            step_push
            echo ""
            read -p "¿Arrancar QEMU ahora? (s/N): " -r
            if [[ $REPLY =~ ^[Ss]$ ]]; then
                step_qemu
            else
                log_warn "QEMU saltado. Ejecuta: $0 --qemu"
            fi
            ;;
        *)
            log_ok "Pipeline completado (build + tests + sync)"
            echo ""
            echo "Para push:     $0 --push"
            echo "Para QEMU:    $0 --qemu"
            echo "Para todo:    $0 --full"
            ;;
    esac
}

main "$@"
