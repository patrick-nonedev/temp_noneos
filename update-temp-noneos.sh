#!/usr/bin/env bash
set -euo pipefail

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
project_root=$(cd -- "$script_dir/../NoneOS" && pwd)
caller_dir=$PWD

if (( $# > 1 )); then
    printf 'Uso: %s [directorio-destino]\n' "$0" >&2
    exit 2
fi

destination=${1:-"$script_dir"}
if [[ "$destination" != /* ]]; then
    destination="$caller_dir/$destination"
fi
if [[ ! -d "$destination" ]]; then
    printf 'No existe el directorio destino: %s\n' "$destination" >&2
    exit 1
fi
if [[ -z "${IN_NIX_SHELL:-}" ]]; then
    printf -v script_q '%q' "$script_dir/$(basename -- "${BASH_SOURCE[0]}")"
    printf -v destination_q '%q' "$destination"
    exec nix-shell --run "bash $script_q $destination_q"
fi

cd -- "$project_root"
rm -f build/libnone-full-musl.a
make build/libnone.a libnone-musl libnone-full-musl noneos-header
musl-gcc -static -fno-link-libatomic -std=c23 -Os \
    -Isrc -Ibuild demo/install_demo.c \
    -Wl,--start-group build/libnone-musl.a \
    build/libnone-full-musl.a \
    -Wl,--end-group -lm -pthread -o build/noneos-install

cp -- build/noneos-install build/libnone.a build/libnone-musl.a \
    build/noneos.h "$destination/"
cp -- demo/config_demo.c demo/hello_noneos.c demo/install_demo.c \
    demo/minimal_demo.c "$destination/"

printf 'Actualizado paquete mínimo en %s\n' "$destination"
