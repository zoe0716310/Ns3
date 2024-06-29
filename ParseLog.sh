#!/bin/bash

# 搜尋以"Receive"開頭的行並匯出成Receive.txt

awk '/^[0-9]+ : /' log.txt > Receive.txt

echo "已匯出以 number : 開頭的行到 Receive.txt"
