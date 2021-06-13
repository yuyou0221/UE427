#!/bin/sh

set -e

# First we check if nodejs is installed
if ! command -v node > /dev/null ; then
  echo "ERROR: Couldn't find node.js installed..., Please install latest nodejs from https://nodejs.org/en/download/"
  exit 1
fi

# Let's check if it is a modern nodejs
VERSION=$(node -e "console.log( process.versions.node.split('.')[0] );")
echo "Found Node.js version ${VERSION}"

if [ ${VERSION} -lt 8 ] ; then
  echo "ERROR: installed node.js version is too old (${VERSION}) :\( Please install latest nodejs from https://nodejs.org/en/download/"
  exit 1
fi

if [ ${VERSION} -gt 14 ] ; then
  echo "ERROR: installed node.js version is not supported (${VERSION}), please install v14 of nodejs from https://nodejs.org/en/download/"
  exit 1
fi

FOLDER=$(dirname "$0")
node ${FOLDER}/Scripts/start.js "$@"

