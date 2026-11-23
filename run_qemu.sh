#!/usr/bin/env bash
# run_qemu.sh — Arranca QEMU con Arch ISO para testeo (TEMPORAL)
# Uso: ./run_qemu.sh          # Arch ISO con ventana gráfica
#        ./run_qemu.sh --boot # Boot del disco instalado
set -euo pipefail

# ============================================================
# Configuración
# ============================================================
DISK_DIR="/mnt/Datos/Maquinas"
DISK="${DISK_DIR}/NoneOS.qcow2"
DISK_SIZE="25G"
ARCH_ISO="/mnt/Datos/Isos/archlinux-2025.09.01-x86_64.iso"
TEMP_ROOT="/mnt/Datos/code/c++/None/temp_noneos"
RAM="4096"
CPUS="2"

mkdir -p "$DISK_DIR"

# ============================================================
# Paso 1: Crear disco si no existe
# ============================================================
create_disk() {
    if [[ -f "$DISK" ]]; then
        echo "==> Disco ya existe: $DISK"
        qemu-img info "$DISK"
        return
    fi
    echo "==> Creando disco $DISK_SIZE..."
    qemu-img create -f qcow2 "$DISK" "$DISK_SIZE"
    echo "==> Disco creado."
}


# ============================================================
# Paso 3: Preparar binarios de NoneOS para la VM
# ============================================================
prepare_binaries() {
    local tmpdir="/tmp/noneos-binaries"
    mkdir -p "$tmpdir"
    cp "$TEMP_ROOT/nshell" "$tmpdir/" 2>/dev/null || true
    cp "$TEMP_ROOT/nshell-musl" "$tmpdir/" 2>/dev/null || true
    cp "$TEMP_ROOT/noneos-config" "$tmpdir/" 2>/dev/null || true
    cp "$TEMP_ROOT/libnone*.a" "$tmpdir/" 2>/dev/null || true
    cp "$TEMP_ROOT/noneos.h" "$tmpdir/" 2>/dev/null || true
    cp "$TEMP_ROOT/install_demo.c" "$tmpdir/" 2>/dev/null || true
    echo "==> Binarios preparados en $tmpdir"
}

run_qemu_install() {
    echo "==> Arrancando Arch ISO (ventana gráfica)"
    qemu-system-x86_64 \
        -enable-kvm \
        -m "$RAM" \
        -smp "$CPUS" \
        -drive "file=$DISK,format=qcow2" \
        -cdrom "$ARCH_ISO" \
        -boot d \
        -serial mon:stdio \
        -device virtio-net-pci,netdev=net0 \
        -netdev user,id=net0,hostfwd=tcp:127.0.0.1:2222-:22
}

run_qemu_boot() {
    echo "==> Boot del disco instalado (ventana gráfica)"
    qemu-system-x86_64 \
        -enable-kvm \
        -m "$RAM" \
        -smp "$CPUS" \
        -drive "file=$DISK,format=qcow2" \
        -boot c \
        -serial mon:stdio \
        -device virtio-net-pci,netdev=net0 \
        -netdev user,id=net0,hostfwd=tcp:127.0.0.1:2222-:22
}

main() {
    local mode="${1:-}"
    case "$mode" in
        --boot)
            [[ -f "$DISK" ]] || { echo "ERROR: Disco no existe"; exit 1; }
            run_qemu_boot
            ;;
        *)
            create_disk
            prepare_binaries
            run_qemu_install
            ;;
    esac
}

main "$@"
