#!/bin/bash

# 定义数组
mean_sending_rate=(30)  # 用你的实际数值替换这些示例值
standard_deviation=(1 2 3 4 5)
seed=(1 2 3 4 5 6 7 8 9 10)
AggregateSsh=(10)
bandwidth=(80)
timeout=(5)

# 遍历数组组合
for mean_rate in "${mean_sending_rate[@]}"; do
  for delta in "${standard_deviation[@]}"; do
    for bw in "${bandwidth[@]}"; do
      for to in "${timeout[@]}"; do
        for s in "${seed[@]}"; do
          for ssh in "${AggregateSsh[@]}"; do
            # 生成目录路径
            dir="logs/0527/50p_0.2ms/Reliable/Timeout_${to}ms/UDP_50bf_${bw}Bw_${mean_rate}_standard_deviation${delta}_delay/seed_${s}"

            # 检查目录是否存在，如果不存在则创建
            if [ ! -d "$dir" ]; then
              mkdir -p "$dir"
            fi
            # 计算变量
            min_rate=$(echo "$mean_rate - $delta" | bc)
            max_rate=$(echo "$mean_rate + $delta" | bc)

            # 生成命令
            command="./ns3 run fifth -- --mean=$mean_rate --standard_deviation=$delta  --timeout=$to  --outputPcapDir=logs/0527/50p_0.2ms/Reliable/Timeout_${to}ms/UDP_50bf_${bw}Bw_${mean_rate}_standard_deviation${delta}_delay/seed_${s} --outgoingBW=$bw --seed=$s --AggregateSsh=$ssh > logs/0527/50p_0.2ms/Reliable/Timeout_${to}ms/UDP_50bf_${bw}Bw_${mean_rate}_standard_deviation${delta}_delay/seed_${s}.txt &"

            echo "$command"
          done
        done
      done
    done
  done
done
