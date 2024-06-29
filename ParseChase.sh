#!/bin/bash

# 搜尋以"Receive"開頭的行並匯出成Receive.txt
grep "SendPacket!" log.txt > Chase.txt

echo "已匯出以 SendPacket 開頭的行到 Chase.txt"

# 指定输入的txt文件
input_file="Chase.txt"

# 循环0到9，根据每个数字生成对应的输出文件
for ((i=0; i<=9; i++)); do
    grep "^Node : $i" "$input_file" | sed -E 's/Node : [0-9]+ SendPacket! Chase Rate : ([0-9.]+) Time : ([0-9.]+)/\2 \1/g' > "ChaseResult/ChaseRate_$i.dat"
done
