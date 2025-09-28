SOURCE="${BASH_SOURCE[0]}"
while [ -h "$SOURCE" ]; do # resolve $SOURCE until the file is no longer a symlink
  DIR="$( cd -P "$( dirname "$SOURCE" )" >/dev/null 2>&1 && pwd )"
  SOURCE="$(readlink "$SOURCE")"
  [[ $SOURCE != /* ]] && SOURCE="$DIR/$SOURCE" # if $SOURCE was a relative symlink, we need to resolve it relative to the path where the symlink file was located
done
SCRIPT_DIR="$( cd -P "$( dirname "$SOURCE" )" >/dev/null 2>&1 && pwd )"

if [ $# -lt 1 ] ; then
  echo "Usage: ./run-docker.sh /path/to/project/dir"
  exit 1
fi

CONTAINER_NAME="gimp-boardgame-component-generator"
TMPDIR=""
PROJECT_DIR="$1"

trap 'EXIT' INT TERM EXIT
EXIT() {
  docker compose down
  if [ -n "$TMPDIR" ] ; then
    mv "$TMPDIR/out" "$PROJECT_DIR/out"
    rm -rf "$TMPDIR"
  fi
  exit
}

set -e

cat > "$SCRIPT_DIR/.env" << EOF
UID=$(id -u)
GID=$(id -g)
TZ=$(cat /etc/timezone)
HOST_PROJECT_DIR="$PROJECT_DIR"
EOF

if [ -d "$PROJECT_DIR/out" ] ; then
  TMPDIR="$(mktemp -d "/tmp/$CONTAINER_NAME.XXXXXXXXXXXX")"
  mv "$PROJECT_DIR/out" "$TMPDIR"
fi
docker compose up --wait
docker compose exec "$CONTAINER_NAME" /run.sh /project_dir
docker compose exec "$CONTAINER_NAME" sudo chown -R $(id -u):$(id -g) /project_dir/out
[ -n "$TMPDIR" ] && rm -rf "$TMPDIR" && TMPDIR=""