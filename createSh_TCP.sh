#!/bin/bash

# 定义数组
mean_bw=(30)  # 用你的实际数值替换这些示例值
deltas=(1 2 3 4 5)
seed=(1 2 3 4 5 6 7 8 9 10)
bandwidth=(80)
aggregatessh=(8)

# 遍历数组组合
for mean_rate in "${mean_bw[@]}"; do
  for delta in "${deltas[@]}"; do
    for bw in "${bandwidth[@]}"; do
      for ssh in "${aggregatessh[@]}"; do
        for s in "${seed[@]}"; do
          # 生成目录路径
          dir="logs/selectedTCP/50p_0.5ms/50bf_${bw}Bw_${mean_rate}_SD_${delta}_ssh_${ssh}/seed_${s}"
          # dir="logs/selectedTCP/50p_0.5ms/50bf_${bw}Bw_${mean_rate}bigN_Alpha_${delta}_6_new_retry/seed_${s}"
          # dir="logs/FullyTCP/50p_0.2ms/Payload_200_60000pkt/50bf_${bw}Bw_${mean_rate}_SD_${delta}/seed_${s}"
          # dir="logs/FullyTCP/50p_0.2ms/50bf_${bw}Bw_${mean_rate}bigN_Alpha_${delta}_big/seed_${s}"

          # 检查目录是否存在，如果不存在则创建
          if [ ! -d "$dir" ]; then
            mkdir -p "$dir"
          fi
        
          # 生成命令
          command="./ns3 run fifth -- --mean=$mean_rate --standard_deviation=$delta --outputPcapDir=$dir --outgoingBW=$bw --seed=$s --AggregateSsh=$ssh > ${dir}/seed${s}.txt &"
          # command="./ns3 run fifth -- --bigN=$mean_rate --alpha=$delta --outputPcapDir=$dir --outgoingBW=$bw --seed=$s > ${dir}/seed${s}.txt &"

          # 打印或执行命令
          echo "$command"
        done
      done
    done
  done
done
