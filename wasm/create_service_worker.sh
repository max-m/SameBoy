#!/bin/sh
BIN_DIR="$1"
shift
VERSION="$1"
shift

if command -v git >/dev/null 2>&1; then
	VERSION="$VERSION"_"$($(command -v git) describe --always --dirty=-modified)"
fi

OUT="$BIN_DIR/service_worker.js"

list=""
for file in "$@"
do
	if [ -d "$file" ]; then
		for f in $(realpath --relative-to="$BIN_DIR" "$file"/*)
		do
			list="$list\t\"$f\",\n"
		done
	else
		list="$list\t\"$(realpath --relative-to="$BIN_DIR" "$file")\",\n"
	fi
done

printf "const VERSION = \"%s\";\n" "$VERSION" > "$OUT"
echo "const FILES = [" >> "$OUT"
echo "$list" >> "$OUT"
printf "];\n" >> "$OUT"

cat web/service_worker.js >> "$BIN_DIR/service_worker.js"
