#!/usr/bin/env bash
print_help() {
    echo "---------- HELP ----------"
    echo
    echo "Dostupné přepínače:"
    echo "  -h             Vypíše nápovědu."
    echo "  -a             Vypíše všechny PDF soubory v aktuálním adresáři."
    echo "  -b             Ze standardního vstupu vypíše řádky začínající číslem, bez čísla."
    echo "  -c             Rozdělí text ze stdin na věty, každá věta na samostatný řádek."
    echo "  -d <prefix>    Přidá prefix do všech #include direktiv v C kódu (ze stdin)."
    echo
    echo "V případě zadání více přepínačů se zpracuje pouze první."
}

if [[ $# -eq 0 ]]; then
    print_help >&2
    exit 1
fi

case "$1" in
    -h)
        print_help
        exit 0
        ;;
        
    -a)
        find . -maxdepth 1 -type f \( -iname "*.pdf" \) -printf "%f\n" | sort
        exit 0
        ;;
        
    -b)
        sed -nE '/^[+-]?[0-9]+/{s/^[+-]?[0-9]+//;p}'
        exit 0
        ;;
        
    -c)
        tr '\n' ' ' | \
        grep -oP '[\p{Lu}][^.!?]*[.!?]'
        exit 0
        ;;
    -d)
    prefix="$2"

    if [[ -z "$prefix" ]]; then
        echo "Chyba: chybí prefix pro přepínač -d" >&2
        print_help >&2
        exit 1
    fi

    sed -E "s|(# *include[[:space:]]*)(\"([^\"<>[:space:]]+)\")|\1\"$prefix\3\"|g; s|(# *include[[:space:]]*)(<([^\"<>[:space:]]+)>)|\1<$prefix\3>|g"
    exit 0
    ;;
    *)
        echo "Neznámý přepínač: $1" >&2
        print_help >&2
        exit 1
        ;;
esac
