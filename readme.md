# 文件信息节点：
*	filename：保存文件的名称，不超过256个字符
*	filelen，存储content的最后一个有效位，即文件内容的最后一个块。
*	begin，存储文件内容的最后一个块中已经使用的字符数。
*	content：存储该文件信息节点包含的所有文件内容在mem中的下标。
*	st：存储文件的相关信息
*	next：链表，指向下一个文件信息节点
*	where：标记该信息节点在mem中的位置
# Block相关
128kb为一个blocksize，总共32*1024个block，共4G内存。
选择128kb作为一个单元，是因为在保证内存大小不变的前提下，如果采用64kb作为一个单元，就无法完整保存下包含有所有节点的数组（需要4*64*1024个字节，即256kb，其余元素均无法有效保存），所以采用128kb为一个单元，此时mem的长度变为32*1024，全部保存需要128kb，但由于部分mem中的段存放文件信息，因此可以将content的长度缩短，此处缩短为31*1024，即允许存放1024个文件信息，同时每个文件的长度不得大于31*1024*BLOCK。
# 实现函数：
*	 init、readdir、getattr、open均复用示例代码
*	unlink： 利用链表的删除方法，先将删除节点从链表中断开，再释放文件内容存储空间和文件信息存储空间。
*	mknod：与示例函数大致相当，增加了时间的存储。
*	create_filenode：首先找到未被使用的内存空间，然后再进行强制类型转换并赋对应的初值，此时filelen和begin均为0。
*	truncate：首先找到对应的文件信息节点，记录修改时间，然后计算size除以BLOCK的商和余数，即得到保留了多少个完整的模块（num）、剩下了多少个字符（size%BLOCK）。则filenode=num+1，begin=size%BLOCK。然后从第num+2个模块开始，删除多余的模块（num+1虽然未被填满但是仍然需要保留）。最后修改filelen。
*	write：首先找到对应的文件节点，count记录总共的模块数（size/BLOCK+1），a1记录第一个被改写的模块（offset/BLOCK），a2记录从该模块的第几个字符开始改写，对于第一个改写的模块和最后一个改写的模块特殊处理，因为此这两个文件内容模块均未完全被改写，中间模块（从a1+1开始）可以用循环处理。
但是还要分写入长度小于原长、长度大于原长两种情况处理，对于小于原长的部分，即从a1+1到num-1的部分，原本的mem就已经分配了空间，所以只需memcpy即可，如果此时count为1，即只剩下一个不完整的模块，则特殊处理。
对于大于原长的部分，必须先找到空闲的空间，再将内容拷贝进去，并修改filenode中对应的conten、filelen，当count为1时，类似处理。（为了防止出现偏移量超过原长导致有一些内存位有定义但没有被赋值，此处在每次mmap后均将所有内容的初值设定为“ ”，并进行适当的分类处理。
*	read：记录还需写的字节数，和BLOCK的长度相比较，根据比较的内容来确定写的长度。
*	寻找空内存块函数：根据空间局部性，从上一个找到空内存块的地方开始寻找下一个空模块。
