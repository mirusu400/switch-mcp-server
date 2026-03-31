#!/usr/bin/env bash
set -e

cd "$(dirname "$0")"

docker build -t switch-mcp-server-builder .
docker create --name switch-mcp-extract switch-mcp-server-builder 2>/dev/null || \
  (docker rm switch-mcp-extract && docker create --name switch-mcp-extract switch-mcp-server-builder)

rm -rf out
docker cp switch-mcp-extract:/src/out ./out
docker rm switch-mcp-extract

echo "[DONE] Build output is in ./out/"
