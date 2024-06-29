#!/bin/bash

# 定义数组
mean_sending_rate=(10 15 20 25 30)  # 用你的实际数值替换这些示例值
standard_deviation=(0.2 0.3 0.4)
seed=(1 2 3 4 5 6 7 8 9 10)
AggregateSsh=(4)
bandwidth=(30)

# 遍历数组组合
for mean_rate in "${mean_sending_rate[@]}"; do
  for delta in "${standard_deviation[@]}"; do
    for bw in "${bandwidth[@]}"; do
      for s in "${seed[@]}"; do
        # 生成目录路径
        dir="logs/0527/50p_5ms/UDP_50bf_30Bw_${mean_rate}_standard_deviation${delta}/seed_${s}"

        # 检查目录是否存在，如果不存在则创建
        if [ ! -d "$dir" ]; then
          mkdir -p "$dir"
        fi

        # 生成命令
        command="./ns3 run fifth -- --mean=$mean_rate --standard_deviation=$delta --outputPcapDir=logs/0527/50p_5ms/UDP_50bf_30Bw_${mean_rate}_standard_deviation${delta}/seed_${s} --outgoingBW=30 --seed=$s --AggregateSsh=4 > logs/0527/50p_5ms/UDP_50bf_30Bw_${mean_rate}_standard_deviation${delta}/seed_${s}.txt &"

        echo "$command"
        # for ssh in "${AggregateSsh[@]}"; do
        #   # 生成命令
        #   command="./ns3 run fifth -- --Bandwidth_min=$min_rate --Bandwidth_max=$max_rate --outputPcapDir=logs/0221/50p_0.5ms/correctDataRate/TCP_Fix_largeData_low_drop_50bf_${bw}Bw_${mean_rate}_delta${delta}/seed_${s}/AggregateSsh${ssh}/ --outgoingBW=$bw --seed=$s --AggregateSsh=$ssh > logs/0221/50p_0.5ms/correctDataRate/TCP_Fix_largeData_low_drop_50bf_${bw}Bw_${mean_rate}_delta${delta}/seed_${s}/AggregateSsh${ssh}.txt &"

        #   # 打印或执行命令
        #   echo "$command"
        # done
      done
    done
  done
done
