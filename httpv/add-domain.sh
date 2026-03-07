#!/bin/bash

CONFIG_DIR=/etc/httpv
HTTPV_DIR=/usr/src/tom7misc/httpv

if [ -z "$1" ]; then
  echo "./add-domain.sh domain.org" >&2
  echo "Works entirely in ${CONFIG_DIR}." >&2
  exit 1
fi

DOMAIN="$1"

TEMPLATEFILE="${CONFIG_DIR}/template.conf"

[[ -f "${TEMPLATEFILE}" ]] || { echo "Missing template file at ${TEMPLATEFILE}."; exit -1; }

CONFIGFILE="${CONFIG_DIR}/${DOMAIN}.conf"

[[ -f "${CONFIGFILE}" ]] && { echo "Config file at ${CONFIGFILE} already exists."; exit -1; }

cat "${TEMPLATEFILE}" | sed -e "s/DOMAIN_NAME/${DOMAIN}/g" > "${CONFIGFILE}"

echo "Edit ${CONFIGFILE} if you need to add more subdomains. "
echo "Only ${DOMAIN} one level of subdomain (wildcard) are in there now."
echo ""
echo "Then for the key ${CONFIG_DIR}/something.key:"
echo "   ./getcert.sh something ${DOMAIN}"
echo ""
echo "This is expected to fail, but will tell you the CNAMEs to add. Do that."
echo "Run it again. It usually takes 30 seconds or so before the CNAMEs resolve."


