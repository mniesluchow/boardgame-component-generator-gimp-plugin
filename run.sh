#!/bin/bash

SOURCE="${BASH_SOURCE[0]}"
while [ -h "$SOURCE" ]; do # resolve $SOURCE until the file is no longer a symlink
  DIR="$( cd -P "$( dirname "$SOURCE" )" >/dev/null 2>&1 && pwd )"
  SOURCE="$(readlink "$SOURCE")"
  [[ $SOURCE != /* ]] && SOURCE="$DIR/$SOURCE" # if $SOURCE was a relative symlink, we need to resolve it relative to the path where the symlink file was located
done
SCRIPT_DIR="$( cd -P "$( dirname "$SOURCE" )" >/dev/null 2>&1 && pwd )"

if [ $# -lt 1 ] ; then
  echo "Usage: ./run.sh /path/to/project/dir"
  exit 1
fi

GIMP_MAJOR_VERSION="$(gimp --version | awk '{print $NF}' | cut -d. -f1)"
GIMPTOOL_BIN="gimptool-$GIMP_MAJOR_VERSION.0"

if ! command -v $GIMPTOOL_BIN >/dev/null 2>&1 ; then
  echo "Installing $GIMPTOOL_BIN"
  sudo apt install -y libgimp$GIMP_MAJOR_VERSION.0-dev
fi

$GIMPTOOL_BIN --install "$SCRIPT_DIR/boardgame-component-generator.c"
if [ $GIMP_MAJOR_VERSION -lt 3 ] ; then
  gimp -i -b "(boardgame-component-generator RUN-NONINTERACTIVE \"$1\")" -b '(gimp-quit 0)'
else
  gimp --batch-interpreter=plug-in-script-fu-eval -i -b "(boardgame-component-generator #:run_mode 1 #:project-dir \"$1\")" -b '(gimp-quit 0)'
fi
$GIMPTOOL_BIN --uninstall-bin boardgame-component-generator