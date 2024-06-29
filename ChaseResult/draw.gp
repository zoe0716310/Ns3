set term gif
set output "ChaseRate2.gif"

# 设置横轴和纵轴的标签
set xlabel "s"
set xrange [0:0.5]
set ylabel "ChaseRate"
set yrange [0:8]

# 绘制第一个.dat文件的折线
plot "ChaseRate_0.dat" title "ChaseRate1" with linespoints,\
"ChaseRate_1.dat" title "ChaseRate2" with linespoints,\
"ChaseRate_2.dat" title "ChaseRate3" with linespoints,\
"ChaseRate_3.dat" title "ChaseRate4" with linespoints,\
"ChaseRate_4.dat" title "ChaseRate5" with linespoints,\
"ChaseRate_5.dat" title "ChaseRate6" with linespoints,\
"ChaseRate_6.dat" title "ChaseRate7" with linespoints,\
"ChaseRate_7.dat" title "ChaseRate8" with linespoints,\
"ChaseRate_8.dat" title "ChaseRate9" with linespoints,\
"ChaseRate_9.dat" title "ChaseRate10" with linespoints