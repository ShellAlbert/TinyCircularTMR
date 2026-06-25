#!/bin/bash

# 定义数据文件和输出文件
DATA_FILE="/tmp/TMR_Phase_A.dat.csv"
OUTPUT_FILE="CSV_Render.png"

# 检查数据文件是否存在
if [ ! -f "$DATA_FILE" ]; then
    echo "错误: 数据文件 $DATA_FILE 不存在!"
    exit 1
fi

# 执行 gnuplot
gnuplot <<EOF
# 1. 设置终端为 pngcairo，并指定输出文件名
# enhanced 允许使用增强文本模式（如上下标），如果不需要可以去掉
set terminal pngcairo size 800,600 enhanced font "Arial,12"
set output "${OUTPUT_FILE}"

# 2. 设置标题和标签
set title "TMR Phase C Curve Rendering"
set xlabel "Time/Samples"
set ylabel "Current/Amps"
set datafile separator ","

# 3. 设置网格和范围 (根据实际数据调整范围，避免自动缩放失真)
set grid
# 如果数据范围已知，建议取消下面两行的注释以固定范围
# set xrange [0:5000]
# set yrange [-1000:75535]

# 4. 绘图命令
# using 1:2 表示使用第1列作为X，第2列作为Y
plot "${DATA_FILE}" using 1:2 with linespoints linewidth 1 pointtype 7 pointsize 0.5 title "AC 50Hz"

# 5. 关闭输出流 (重要，确保文件完整写入)
set output
EOF

# 检查 gnuplot 是否成功执行
if [ $? -eq 0 ]; then
    echo "成功生成图片: ${OUTPUT_FILE}"
else
    echo "错误: Gnuplot 执行失败"
    exit 1
fi




