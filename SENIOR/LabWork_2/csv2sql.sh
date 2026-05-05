#!/bin/bash

INFILE=$1
OUTFILE=${2:-output.sql}

if [[ ! -f "$INFILE" ]]; then
    echo "Использование: $0 <файл.csv> [выходной_файл.sql]"
    exit 1
fi

TABLE_NAME=$(sed -n '1p' "$INFILE" | xargs)
COLUMNS=$(sed -n '2p' "$INFILE" | xargs)

awk -v table="$TABLE_NAME" -v cols="$COLUMNS" -F',' '
NR > 2 && NF > 0 {
    val_list = ""
    for (i = 1; i <= NF; i++) {
        gsub(/^[ \t]+|[ \t]+$/, "", $i)
        
        if ($i == "" || $i == "NULL") {
            v = "NULL"
        } else if ($i ~ /^-?[0-9.]+$/) {
            v = $i
        } else {
            gsub(/\047/, "\047\047", $i)
            v = "\047" $i "\047"
        }
        val_list = (val_list == "" ? "" : val_list ", ") v
    }
    printf "INSERT INTO %s (%s) VALUES (%s);\n", table, cols, val_list
}' "$INFILE" > "$OUTFILE"
