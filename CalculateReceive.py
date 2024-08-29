def calculate_receive_percentage(data_file):
    # 读取文件
    with open(data_file, 'r') as file:
        lines = file.readlines()

    total_lines = len(lines)
    receiver_data = {f"Receiver {i+1}": 0 for i in range(10)}
    all_received_count = 0
    ones_count_distribution = {i: 0 for i in range(11)}  # 用於記錄1的數量為0到10個的次數

    # 统计每个接收器收到数据的次数
    for line in lines:
        # print(f"Processing line: {line}")
        line_data = line.strip().split(":")[1].strip()
        ones_count = line_data.count('1')  # 計算1的個數
        ones_count_distribution[ones_count] += 1  # 更新對應的計數

        for i, digit in enumerate(line_data):
            if digit == '1':
                receiver_data[f"Receiver {i+1}"] += 1

        # 检查所有接收器是否都收到了数据
        if '0' not in line_data:
            all_received_count += 1

    # 计算每个接收器收到数据的百分比
    receiver_percentage = {receiver: (count / total_lines) * 100 for receiver, count in receiver_data.items()}

    # 计算所有接收器都收到数据的比例
    all_received_percentage = (all_received_count / total_lines) * 100

    # 计算1的数量在0到10之間的比例
    ones_count_percentage = {count: (times / total_lines) * 100 for count, times in ones_count_distribution.items()}

    return receiver_percentage, all_received_percentage, ones_count_percentage

# 测试代码
data_file = "Receive.txt"
receiver_percentage, all_received_percentage, ones_count_percentage = calculate_receive_percentage(data_file)

print("Receiver percentage:")
for receiver, percentage in receiver_percentage.items():
    print(f"{receiver}: {percentage:.2f}%")
print(f"All receivers received data percentage: {all_received_percentage:.2f}%")

print("Ones count distribution percentage:")
for count, percentage in ones_count_percentage.items():
    print(f"{count} ones: {percentage:.2f}%")
