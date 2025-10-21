#!/usr/bin/env bash

files_to_archive=()
error_found=0

for arg in "$@"; do
    if [[ "$arg" == "-h" ]]; then
        echo "HELP CENTER"
        echo "Simple script to show info about files, directories, and links."
        exit 0
    elif [[ "$arg" == "-z" ]]; then
        :
    else
        echo "Unknown argument: $arg" >&2
        exit 2
    fi
done

while IFS= read -r line; do
    if [[ "$line" == PATH* && "$line" == "PATH "* ]]; then
        file="${line#PATH }"
        if [ -L "$file" ]; then
            target=$(readlink "$file")
            echo "LINK '$file' '$target'"
        elif [ -d "$file" ]; then
            echo "DIR '$file'"
        elif [ -f "$file" ]; then
            if ! first_line=$(head -n 1 "$file" 2>/dev/null); then
                echo "ERROR '$file'" >&2
                exit 2
            fi
            lines=$(wc -l < "$file" 2>/dev/null)
            [[ -z "$lines" ]] && lines=0
            echo "FILE '$file' $lines '$first_line'"
            files_to_archive+=("$file")
        else
            echo "ERROR '$file'" >&2
            error_found=1
        fi
    fi
done

if [[ "${!#}" == "-z" && ${#files_to_archive[@]} -gt 0 ]]; then
    if ! tar czf output.tgz "${files_to_archive[@]}" 2>/dev/null; then
        echo "ERROR creating archive" >&2
        exit 2
    fi
fi

if [[ $error_found -eq 1 ]]; then
    exit 1
else
    exit 0
fi
