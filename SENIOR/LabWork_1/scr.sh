#!/bin/bash

OUTFILE="result.txt" 
HEADER="#!/bin/bash" 
TOTAL_SUM=0               

> "$OUTFILE"

ALL_FILES=$(find . -type f)

for FILE in $ALL_FILES; do

    FILENAME=$(basename "$FILE")
    MATCH=false

    if [[ "$1" == "-r" && "$FILENAME" =~ $2 ]]; then
        MATCH=true

    elif [[ "$1" == "-l" ]]; then

        for ARG in "${@:2}"; do
            if [[ "$FILENAME" == "$ARG" ]]; then
                MATCH=true
                break
            fi
        done
    fi

    if [[ "$MATCH" == false ]]; then
        continue
    fi

    FIRST_LINE=$(head -n 1 "$FILE" 2>/dev/null)
    if [[ "$FIRST_LINE" == "$HEADER" ]]; then
        echo "Заголовок найден. Содержимое:" >> "$OUTFILE"
        nl -ba "$FILE" >> "$OUTFILE" # nl нумерует строки
        echo "" >> "$OUTFILE"
    fi

    FILE_SUM=$(od -An -tu1 "$FILE" | awk '{for(i=1;i<=NF;i++) s+=$i} END{print s+0}')
    TOTAL_SUM=$((TOTAL_SUM + FILE_SUM))
    
    echo "Сумма байтов в файле $FILE: $FILE_SUM"
echo "Общая сумма байтов: $TOTAL_SUM"
done