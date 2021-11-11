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
fi

gimptool-2.0 --install "$SCRIPT_DIR/boardgame-component-generator.c"
gimp -i -b "(boardgame-component-generator RUN-NONINTERACTIVE 0 0 \"$1\")" -b '(gimp-quit 0)'
gimptool-2.0 --uninstall-bin boardgame-component-generator