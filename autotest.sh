#!/usr/bin/env bash
set -u

ISO="demo_iso/Cervus.latest.iso"
COUNT=25
LOG_DIR="autotest_$(date +%Y%m%d_%H%M%S)"
mkdir -p "$LOG_DIR"

SUCCESS="All tests finished"

CRASH_PATTERNS=(
    "Invalid Opcode"
    "CRITICAL:.*halted"
    "Kernel panic"
    "triple fault"
    "double fault"
    "\[FAIL\]"
    "SMP WARNING.*Only 0/"
    "halted"
)

echo "Тест Cervus OS — $COUNT запусков"
echo "ISO:          $ISO"
echo "Логи:         $LOG_DIR"
echo "───────────────────────────────────────────────"

success=0
failed=()

for i in $(seq -w 1 "$COUNT"); do
    log="$LOG_DIR/run-$i.txt"
    echo -n "Запуск $i ... "

    > "$log"

    qemu-system-x86_64 \
        -cdrom "$ISO" \
        -boot d \
        -serial file:"$log" \
        -m 8G \
        -smp 8 \
        -cpu qemu64,+fsgsbase \
        -nographic \
        -monitor none \
        -display none \
        2>/dev/null &

    qpid=$!

    start_time=$(date +%s)
    max_time=30
    silence_threshold=8

    last_size=0
    silence_start=""
    detected_crash=""

    while kill -0 "$qpid" 2>/dev/null; do
        now=$(date +%s)

        if (( now - start_time > max_time )); then
            kill -9 "$qpid" 2>/dev/null
            detected_crash="таймаут ($max_time с) — система не завершилась"
            break
        fi

        cur_size=$(stat -c %s "$log" 2>/dev/null || echo 0)

        if (( cur_size > last_size )); then
            last_size=$cur_size
            silence_start=""
            for pat in "${CRASH_PATTERNS[@]}"; do
                if grep -qiE "$pat" "$log" 2>/dev/null; then
                    detected_crash="обнаружено: $pat"
                    kill -9 "$qpid" 2>/dev/null
                    break 2
                fi
            done
        else
            if [[ -z "$silence_start" ]]; then
                silence_start=$now
            fi

            if (( now - silence_start >= silence_threshold )); then
                kill -9 "$qpid" 2>/dev/null
                detected_crash="лог замер на $silence_threshold+ секунд"
                break
            fi
        fi

        sleep 0.5
    done

    wait "$qpid" 2>/dev/null

    if grep -q "$SUCCESS" "$log" 2>/dev/null && \
       ! grep -qiE "$(IFS="|"; echo "${CRASH_PATTERNS[*]}")" "$log" 2>/dev/null; then
        echo "OK"
        success=$((success + 1))
        # rm -f "$log"
    else
        reason="${detected_crash:-нет 'All tests finished' и нет явного краша}"
        echo "FAIL → $log"
        echo "   Причина: $reason"
        failed+=("$i")
    fi
done

echo "───────────────────────────────────────────────"
echo "Успешно   : $success / $COUNT"
echo "Неудачно  : ${#failed[@]}"

if (( ${#failed[@]} > 0 )); then
    echo
    echo "Неудачные запуски:"
    for n in "${failed[@]}"; do
        echo "  $n → $LOG_DIR/run-$n.txt"
    done
    echo
    echo "Все неудачные одной командой:"
    echo "  less $LOG_DIR/run-*.txt"
else
    echo
    echo "Все $COUNT запусков прошли успешно!"
fi

echo "Готово."