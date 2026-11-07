#!/bin/zsh
# Extrae todo el contenido de Mutiny.swf con JPEXS a build/extract.
# Uso: tools/extract.sh [ruta/al/Mutiny.swf]
set -e
P="${0:A:h:h}"
SWF="${1:-$P/Mutiny.swf}"
OUT="$P/build/extract"
FFDEC="$P/tools/third_party/ffdec/ffdec.jar"
export JAVA_HOME="${JAVA_HOME:-/opt/homebrew/opt/openjdk}"
export PATH="$JAVA_HOME/bin:$PATH"
[ -f "$FFDEC" ] || { echo "Falta JPEXS en $FFDEC"; exit 1; }
rm -rf "$OUT"; mkdir -p "$OUT"
cp "$SWF" "$OUT/Mutiny.swf"
cd "$OUT"
echo "[extract] XML"
java -jar "$FFDEC" -swf2xml Mutiny.swf swf.xml > log_xml.txt 2>&1
echo "[extract] scripts, bitmaps, sonidos"
java -jar "$FFDEC" -format script:as,image:png,sound:wav -export script,image,sound . Mutiny.swf > log_export.txt 2>&1
echo "[extract] formas a 1x"
java -jar "$FFDEC" -format shape:png,morphshape:svg -zoom 1 -export shape,morphshape . Mutiny.swf > log_shapes.txt 2>&1
echo "[extract] listo: $(find . -type f | wc -l | tr -d ' ') archivos"
