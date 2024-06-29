def calculate_receive_percentage(data_file):
    # 读取文件
    with open(data_file, 'r') as file:
        lines = file.readlines()

    total_lines = len(lines)
    receiver_data = {f"Receiver {i+1}": 0 for i in range(10)}
    all_received_count = 0

    # 统计每个接收器收到数据的次数
    for line in lines:
        print(f"Processing line: {line}")
        line_data = line.strip().split(":")[1].strip()
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

    return receiver_percentage, all_received_percentage

# 测试代码
data_file = "Receive.txt"
receiver_percentage, all_received_percentage = calculate_receive_percentage(data_file)
print("Receiver percentage:")
for receiver, percentage in receiver_percentage.items():
    print(f"{receiver}: {percentage:.2f}%")
print(f"All receivers received data percentage: {all_received_percentage:.2f}%")