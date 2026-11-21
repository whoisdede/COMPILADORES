#!/usr/bin/env bash
set -u

PASS=0
FAIL=0

mkdir -p build/test_out

echo "================ TESTES POSITIVOS (Python -> Jasmin -> JVM) ================"
for pyfile in tests/test*.py; do
    base=$(basename "$pyfile" .py)
    exp="tests/${base}.expected"
    cls="Test_${base}"
    jfile="build/test_out/${cls}.j"
    outfile="build/test_out/${cls}.out"

    if ! ./pycomp "$pyfile" -o "$jfile" -c "$cls" > "build/test_out/${cls}.comp.log" 2>&1; then
        echo "[FAIL] $base: erro na compilacao"
        cat "build/test_out/${cls}.comp.log"
        FAIL=$((FAIL + 1))
        continue
    fi

    if ! jasmin -d build/test_out "$jfile" > "build/test_out/${cls}.jasmin.log" 2>&1; then
        echo "[FAIL] $base: erro no Jasmin"
        cat "build/test_out/${cls}.jasmin.log"
        FAIL=$((FAIL + 1))
        continue
    fi

    if ! java -cp build/test_out "$cls" > "$outfile" 2>&1; then
        echo "[FAIL] $base: erro na execucao JVM"
        cat "$outfile"
        FAIL=$((FAIL + 1))
        continue
    fi

    if diff -u "$exp" "$outfile" > "build/test_out/${cls}.diff"; then
        echo "[PASS] $base"
        PASS=$((PASS + 1))
    else
        echo "[FAIL] $base: saida divergente"
        cat "build/test_out/${cls}.diff"
        FAIL=$((FAIL + 1))
    fi
done

echo ""
echo "================ TESTES NEGATIVOS (Erros esperados) ================"
for errfile in tests/errors/err*.py; do
    base=$(basename "$errfile" .py)
    log="build/test_out/${base}.err.log"

    if ./pycomp "$errfile" -o "build/test_out/${base}.j" -c "Err_${base}" > "$log" 2>&1; then
        echo "[FAIL] $base: compilou mas deveria ter falhado!"
        FAIL=$((FAIL + 1))
    else
        msg=$(grep -m1 "ERRO" "$log" || head -n 1 "$log")
        echo "[PASS] $base -> $msg"
        PASS=$((PASS + 1))
    fi
done

echo ""
echo "Resumo: $PASS passaram, $FAIL falharam."
if [ "$FAIL" -ne 0 ]; then
    exit 1
fi
