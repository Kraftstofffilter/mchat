#!/usr/bin/env bash
# mchat-cleanup.sh
#
# Deletes downloaded attachments older than MCHAT_CLEANUP_DAYS (default 30)
# from nchat's profile folders, mchat's attachment link folder and its
# thumbnail cache.
# nchat shows a deleted attachment as downloadable again, so a click
# fetches it anew. Session data is never touched: WhatsApp files are only
# taken from <profile>/tmp, Telegram files only from subfolders of
# <profile>/tdlib (never its top-level db.sqlite or td.binlog).

set -euo pipefail

DAYS="${MCHAT_CLEANUP_DAYS:-30}"
CONF_DIR="${MCHAT_CONF_DIR:-${HOME}/.config/nchat}"
LINK_DIR="${MCHAT_LINK_DIR:-${HOME}/.local/share/mchat/files}"

shopt -s nullglob
for dir in "${CONF_DIR}"/profiles/WhatsAppMd_*/tmp; do
  find "${dir}" -type f -mtime "+${DAYS}" -print -delete
done

for dir in "${CONF_DIR}"/profiles/Telegram_*/tdlib; do
  find "${dir}" -mindepth 2 -type f -mtime "+${DAYS}" -print -delete
done

THUMB_DIR="${HOME}/.cache/mchat/thumbs"
if [[ -d "${THUMB_DIR}" ]]; then
  find "${THUMB_DIR}" -type f -mtime "+${DAYS}" -print -delete
fi

if [[ -d "${LINK_DIR}" ]]; then
  find "${LINK_DIR}" -mindepth 1 -type f -mtime "+${DAYS}" -print -delete
  find "${LINK_DIR}" -mindepth 1 -type d -empty -delete
fi
