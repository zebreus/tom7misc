#!/bin/bash

DIR=/etc/httpi

if [ -z "$1" ] || [ -z "$2" ]; then
  echo "./getcert.sh key_name config_name" >&2
  echo "Works entirely in ${DIR}." >&2
  exit 1
fi

KEYNAME="$1"
CONFIGNAME="$2"
KEYFILE="${DIR}/${KEYNAME}.key"
CONFIGFILE="${DIR}/${CONFIGNAME}.conf"
CHAINFILE="${DIR}/${KEYNAME}.${CONFIGNAME}.chain"

TMPDIR=`mktemp -d`
TRASH1="${TMPDIR}/trash1.pem"
TRASH2="${TMPDIR}/trash2.pem"
CSRFILE="${TMPDIR}/request.csr"

# generate CSR
openssl req -new -key "${KEYFILE}" -config "${CONFIGFILE}" -out "${CSRFILE}"

# Sign the CSR and keep just the full chain PEM file.
# Try with --debug-challenges if debugging.
certbot certonly --agree-tos --manual --manual-auth-hook /etc/letsencrypt/acme-dns-auth.py --preferred-challenges dns --csr "${CSRFILE}" --chain-path "${TRASH1}" --cert-path "${TRASH2}" --fullchain-path "${CHAINFILE}"

rm -f "${TRASH1}" "${TRASH2}"

echo "CSR was ${CSRFILE}. You can delete it. It is not secret."
echo "Wrote to: ${CHAINFILE}"

