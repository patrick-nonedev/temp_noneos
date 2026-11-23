#!/usr/bin/env bash
# push.sh — Commit + push de los archivos actualizados a temp_noneos
# Uso: ./push.sh [mensaje]
set -euo pipefail

cd /mnt/Datos/code/c++/None/temp_noneos

MSG="${1:-Update binaries and glue fixes}"

echo "==> Estado del repo temp_noneos..."
git status --short

echo "==> Añadiendo archivos..."
git add -A

# Verificar que hay cambios
if git diff --cached --quiet; then
    echo "==> No hay cambios para push."
    exit 0
fi

echo "==> Commit: $MSG"
git commit -m "$MSG"

echo "==> Push a origin..."
git push origin main 2>/dev/null || git push origin master 2>/dev/null || {
    echo "==> Push falló. Verifica la rama actual:"
    git branch --show-current
    BRANCH=$(git branch --show-current)
    git push origin "$BRANCH"
}

echo "==> Push completado."
