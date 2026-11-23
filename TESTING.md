# NoneOS — Scripts de Testeo Automatizado

## Scripts disponibles

| Script | Propósito |
|--------|-----------|
| `sync_files.sh` | Copia binarios y headers actualizados desde `NoneOS/build/` |
| `push.sh` | Commit + push de los archivos sincronizados al repo remoto |
| `run_qemu.sh` | Crea disco virtual y arranca QEMU con Arch ISO para testeo |
| `test_all.sh` | Pipeline completo: build → tests → sync → push → QEMU |

## Flujo rápido

```bash
# Opción 1: Pipeline completo (build + tests + sync)
./test_all.sh

# Opción 2: Con push incluido
./test_all.sh --push

# Opción 3: Todo + QEMU interactivo
./test_all.sh --full

# Opción 4: Solo arrancar QEMU (si ya tienes disco instalado)
./run_qemu.sh --boot
```

## Flujo detallado

### 1. Sincronizar binarios
```bash
./sync_files.sh
```
Copia desde `NoneOS/build/`:
- `nshell`, `nshell-musl`, `noneos-config`, `miniinit`
- `libnone.a`, `libnone-musl.a`, `libnone-full-glibc.a`, `libnone-full-musl.a`
- `noneos.h`
- Fuentes de glue: `glue.c`, `glue.h`, `mini-init.c`, `configure.c`

### 2. Push al repo
```bash
./push.sh "mensaje opcional"
```
Hace `git add -A`, commit y push a la rama actual.

### 3. Arrancar QEMU
```bash
# Primera vez: crea disco y arranca Arch ISO
./run_qemu.sh

# Después de instalar: boot directo
./run_qemu.sh --boot

# Headless (background)
./run_qemu.sh --headless
```

#### Dentro de la VM (Arch ISO)
```bash
# Particionar
parted /dev/vda --script mklabel gpt
parted /dev/vda --script mkpart primary fat32 1MiB 513MiB
parted /dev/vda --script set 1 esp on
parted /dev/vda --script mkpart primary ext4 513MiB 100%

# Formatear
mkfs.vfat -F32 /dev/vda1
mkfs.ext4 /dev/vda2

# Montar
mount /dev/vda2 /mnt
mkdir -p /mnt/boot/efi
mount /dev/vda1 /mnt/boot/efi

# Instalar base
pacstrap -K /mnt base linux linux-firmware util-linux e2fsprogs dosfstools limine
genfstab -U /mnt > /mnt/etc/fstab

# Chroot y configurar
arch-chroot /mnt
echo 'noneos-test' > /etc/hostname
echo 'root:root' | chpasswd
limine-install --bootloader-id=limine --root=/

# Configurar limine.conf con console=ttyS0
mkdir -p /boot/efi/EFI/limine
cat > /boot/efi/EFI/limine/limine.conf <<'EOF'
:NoneOS Test
  PROTOCOL=linuxboot
  KERNEL_PATH=boot:///vmlinuz-linux
  MODULE_PATH=boot:///initramfs-linux.img
  CMDLINE=console=tty0 console=ttyS0 root=/dev/vda2 rw
EOF

exit
umount -R /mnt
reboot
```

### 4. Verificar los fixes de glue

Después de boot, verificar:

```bash
# runit: agetty-tty1..tty6 con run scripts
ls -la /etc/sv/agetty-tty*/run

# udevd funcionando
ls -la /dev/tty1 /dev/tty2

# agetty corriendo
ps aux | grep agetty
```

## Configuración

Los paths se pueden modificar en cada script:

| Variable | Valor por defecto |
|----------|-------------------|
| `NONEOS_ROOT` | `/mnt/Datos/code/c++/None/NoneOS` |
| `TEMP_ROOT` | `/mnt/Datos/code/c++/None/temp_noneos` |
| `DISK_DIR` | `/mnt/Datos/Maquinas` |
| `ARCH_ISO` | `/mnt/Datos/Isos/archlinux-2025.09.01-x86_64.iso` |
| `DISK` | `${DISK_DIR}/NoneOS.qcow2` (25G) |
| `RAM` | 4096 MB |
| `CPUS` | 2 |

## Notas

- El disco se crea en `/mnt/Datos/Maquinas/NoneOS.qcow2` (25G qcow2).
- QEMU usa KVM (`-enable-kvm`) y serial console (`-nographic`).
- SSH forwarding: `ssh -p 2222 root@localhost` (password: `root`).
- Para salir de QEMU: `Ctrl+A X` o `Ctrl+Alt+2` → `quit`.
