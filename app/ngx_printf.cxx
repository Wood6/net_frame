// 
#include "ngx_global.h"
#include "ngx_macro.h"
#include "ngx_func.h"

// 只用于本文件的一些函数声明就放在本文件中
static u_char* SprintfNum(u_char* p_buf, u_char* p_last, uint64_t ui64, u_char zero, uintptr_t hexadcimal, uintptr_t width);


/**
 * 功能：
	对于 nginx 自定义的数据结构进行标准格式化输出,
	就像 printf,vprintf 一样，我们顺道学习写这类函数到底内部是怎么实现的

 * 输入参数：(u_char* p_buf, u_char* p_last, const char* fmt, va_list args)
	p_buf 往这里放按格式转换后的数据
	p_last 放的数据不要超过这里
	fmt 挨着可变参数的一个普通函数，其中可能包含有控制字符
	... 变参，参数个数与fmt中的格式化控制字%数量相等，若不相等会出错

 * 返回值：
	实质就是函数执行完时的p_buf指针，即转换放好的数据后面一个指针位置

 * 调用了函数：
	VslPrintfNum()

 * 其他说明：
	该函数只不过相当于针对 VslPrintf() 函数包装了一下，所以，直接研究 VslPrintf() 即可

 * 例子说明：

 */
u_char* SlPrintf(u_char* p_buf, u_char* p_last, const char* fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	u_char* p = VslPrintf(p_buf, p_last, fmt, args);
	va_end(args);

	return p;
}

/**
 * 功能：
	和上边的 SlPrintf非常类似，只是将末尾位置直接指定改为加个最大偏移量max指定
	类printf()格式化函数，比较安全，max指明了缓冲区结束位置

 * 输入参数：(u_char *p_buf, size_t max, const char *fmt, ...)   
	p_buf 往这里放按格式转换后的数据
	max 相对于起始点上指明了缓冲区结束位置
	fmt 挨着可变参数的一个普通函数，其中可能包含有控制字符
	... 变参，参数个数与fmt中的格式化控制字%数量相等，若不相等会出错

 * 返回值：
	实质就是函数执行完时的p_buf指针，即转换放好的数据后面一个指针位置

 * 调用了函数：
	VslPrintfNum()

 * 其他说明：
	该函数只不过相当于针对 VslPrintf() 函数包装了一下，所以，直接研究 VslPrintf() 即可

 * 例子说明：

 */
u_char * SnPrintf(u_char *p_buf, size_t max, const char *fmt, ...)
{
	u_char   *p;
	va_list   args;

	va_start(args, fmt);
	p = VslPrintf(p_buf, p_buf + max, fmt, args);
	va_end(args);
	return p;
}


/**
 * 功能：
	将一段字符串数据按此函数定义的格式标准化输出
	支持的格式： %d【%Xd/%xd】:数字,    %s:字符串      %f：浮点,  %P：pid_t

 * 输入参数：(u_char* p_buf, u_char* p_last, const char* fmt, va_list args)
	p_buf 往这里放按格式转换后的数据
	p_last 放的数据不要超过这里
	fmt 挨着可变参数的一个普通函数，其中可能包含有控制字符
	args 可变参数

 * 返回值：
	实质就是函数执行完时的p_buf指针，即转换放好的数据后面一个指针位置
 * 调用了函数：
	SprintfNum()

 * 其他说明：
	args是上一个函数的参数...经过va_start指定后带到这个函数来的
	对于：LogStderr(0, "invalid option: \"%s\",%d", "testinfo",123);
	  fmt = "invalid option: \"%s\",%d"
	  args = "testinfo",123

 * 例子说明：
	此函数由LogStderr()调用，这这个调用函数最后两个参数就是fmt，args，用例如下
	LogStderr(0, "invalid option: \"%s\"", argv[0]);  //nginx: invalid option: "./nginx"
	LogStderr(0, "invalid option: %10d", 21);         //nginx: invalid option:         21  ---21前面有8个空格
	LogStderr(0, "invalid option: %.6f", 21.378);     //nginx: invalid option: 21.378000   ---%.这种只跟f配合有效，往末尾填充0
	LogStderr(0, "invalid option: %.6f", 12.999);     //nginx: invalid option: 12.999000
	LogStderr(0, "invalid option: %.2f", 12.999);     //nginx: invalid option: 13.00
	LogStderr(0, "invalid option: %xd", 1678);        //nginx: invalid option: 68e
	LogStderr(0, "invalid option: %Xd", 1678);        //nginx: invalid option: 68E
	LogStderr(15, "invalid option: %s , %d", "testInfo",326);        //nginx: invalid option: testInfo , 326
 */
u_char* VslPrintf(u_char* p_buf, u_char* p_last, const char* fmt, va_list args)
{
	/*
	#ifdef _WIN64
		typedef unsigned __int64  uintptr_t;
	#else
		typedef unsigned int uintptr_t;
	#endif
	*/
	// 定义一些辅助保存格式化的相关数据的局部变量，集中定义在这里
	uintptr_t width = 0;         // 对于%10d,%16f这种格式，此变量就是保存%后面那个10或者16用的
	uintptr_t sign = 0;          // 标识是否有符合，1有符合，0无符合
	uintptr_t hex = 0;           // 标识是否以16进制显示，0不是，1是并小写格式显示，2是并大写字母显示
	uintptr_t frac_width = 0;    // 小数点后面位数，一般需要和%.10f配合使用，这里10就是frac_width；
	uintptr_t scale = 0;         // 位数乘积因子，个位乘1，十位乘10，百位乘100 。。。

	// 定义一些局部变量，保存字符按格式控制类型转换后的数据
	int64_t i64 = 0;             // 保存%d对应的可变参
	uint64_t ui64 = 0;           // 保存%ud对应的可变参，临时作为%f可变参的整数部分也是可以的 
	u_char *p_str = NULL;        // 保存%s对应的可变参
	double f = 0;                // 保存%f对应的可变参
	uint64_t frac = 0;           // 保存%f小数部分内容，如根据%.2f等，就是提取小数部分的2位后的内容保存到frac中

	u_char zero = '0';           // 若%后面第一个字符是'0' 或者' '，将其保存下来作为后面处理宽度不足时的填充字符
								 // 这里缺省值赋'0'


	// 以fmt中的字符为依据，一个个字节的循环读取来进行处理
	while ((*fmt) && (p_buf < p_last))
	{
		// 读到格式控制字的标记%了
		if ('%' == *fmt)
		{
			// 格式辅助变量集中在这每次开始使用时给其初始化一下
			width = 0;
			sign = 1;         // 缺省为有符合数，除非格式控制字使用%u或者16进制数将其赋为0 
			hex = 0;
			frac_width = 0;

			i64 = 0;
			ui64 = 0;


			++fmt;   // 指向%后第一个字符
			zero = (u_char)((*fmt == '0') ? '0' : ' ');   // 格式化带宽度时看是否指定了填充字符，若有将其保存下来

			while ((*fmt >= '0') && (*fmt <= '9'))    // 如果%后边接的字符是 '0' --'9'之间的内容，比如%16这种；
			{
				width = width * 10 + (*fmt - '0');
				++fmt;
			}

			// 
			while (1)
			{
				switch (*fmt)
				{
				case 'u':   // %u，这个u表示无符号
					sign = 0;
					++fmt;
					continue;

				case 'x':   // %x，x表示十六进制，并且十六进制中的a-f以小写字母显示，不要单独使用，一般是%xd
					hex = 1;
					sign = 0;
					++fmt;
					continue;

				case 'X':   // %X，X表示十六进制，并且十六进制中的A-F以大写字母显示，不要单独使用，一般是%Xd
					hex = 2;
					sign = 0;
					++fmt;
					continue;

					// 其后边必须跟个数字，必须与%f配合使用，形如 %.10f：表示转换浮点数时小数部分的位数，
					// 比如%.10f表示转换浮点数时，小数点后必须保证10位数字，不足10位则用0来填补；
				case '.':
					++fmt;
					while ((*fmt >= '0') && (*fmt <= '9'))
					{
						frac_width = frac_width * 10 + (*fmt - '0');
						++fmt;
					}

				default:
					break;
				}
				// 这行很容易被忽略掉，导致程序陷入死循环，如解析%d时没有下面行就死循环了
				break;
			}

			switch (*fmt)
			{
			case '%':      // 只有%%时才会遇到这个情形，本意是打印一个%，所以
				*p_buf++ = '%';
				++fmt;
				continue;   // 调到下一个while循环执行

			case 'd':       // 显示整型数据，如果和u配合使用，也就是%ud,则是显示无符号整型数据
				if (sign)
				{
					// va_arg():遍历可变参数，var_arg的第二个参数表示遍历的这个可变的参数的类型
					i64 = (int64_t)va_arg(args, int);
				}
				else         // 和 %ud配合使用，则本条件就成立
				{
					ui64 = (uint64_t)va_arg(args, u_int);
				}
				break;       // switch中break，跳出这个switch

			case 's':        //  一般用于显示字符串
				p_str = va_arg(args, u_char*);
				while ((*p_str) && (p_buf < p_last))
				{
					*p_buf++ = *p_str++;
				}
				++fmt;
				continue;


			case 'p':        // 与大写P再区分开来，这个小写p表示格式化输出指针值，即一个地址值
				ui64 = (uintptr_t)va_arg(args, void *);
				hex = 2;     // 标记以大写字母显示十六进制中的A-F
				sign = 0;    // 标记这是个无符号数
				zero = '0';  // 前边0填充
				width = 2 * sizeof(void *);
				break;

			case 'P':         // 转换一个pid_t类型,大小写都可以，即%p和%P都是可以读取进程号的
				i64 = (int64_t)va_arg(args, pid_t);
				sign = 1;
				break;

				// 对于浮点数处理要复杂一点，因为设计到小数位数，还有小数位四舍五入处理
			case 'f':         //  一般用于显示double类型数据，如果要显示小数部分，则要形如%.5f
			{
				f = va_arg(args, double);
				if (f < 0)     // 负数的处理
				{
					*p_buf++ = '-';
					f = -f;
				}
				ui64 = (int64_t)f;    // 取整给下面提取小数用
				frac = 0;

				// 如果要求小数点后显示多少位小数,前面根据'.'已经处理取得了frac_width数据
				// 比如是%d.2f，那么经过前面的处理到这里时frac_width就是2了
				if (frac_width)
				{
					scale = 1;    // 乘积因子
					for (int n = frac_width; n > 0; --n)
					{
						scale *= 10;
					}

					//  把小数部分取出来 ，比如若参数是12.537，格式是    %.2f
					// (uint64_t) ((12.537 - (double) 12) * 100 + 0.5);  
					// = (uint64_t) (0.537 * 100 + 0.5) 
					// = (uint64_t) (53.7 + 0.5) 
					// = (uint64_t) (54.2) 
					// = 54
					// 这样就成功按四舍五入处理了小数
					// 并将这个处理后得到的小数部分数据保存在变量frac
					frac = (uint64_t)((f - (double)ui64) * scale + 0.5);

					// 进位，比如    %.2f, 对应的参数是12.999, 那么
					// frac = (uint64_t) (0.999 * 100 + 0.5)  
					// = (uint64_t) (99.9 + 0.5) = (uint64_t) (100.4) = 100
					// 而此时scale == 100，两者正好相等
					if (frac == scale)
					{
						++ui64;
						frac = 0;
					}
				}

				//  正整数部分，先显示出来
				p_buf = SprintfNum(p_buf, p_last, ui64, zero, 0, width);
				if (frac_width)
				{
					if (p_buf < p_last)
					{
						*p_buf++ = '.';

					}
					p_buf = SprintfNum(p_buf, p_last, frac, '0', 0, frac_width);
				}

				++fmt;
				continue;
			}

			default:
				*p_buf++ = *fmt++;
				continue;
			}

			// 统一把显示的数字都保存到 ui64 里去；
			if (sign)
			{
				if (i64 < 0)     //  这可能是和%d格式对应的要显示的数字
				{
					*p_buf++ = '-';           // 小于0，自然要把负号先显示出来
					ui64 = (uint64_t)(-i64);  //  变成无符号数（正数）
				}
				else  // 显示正数
				{
					ui64 = (uint64_t)i64;
				}
			}

			p_buf = SprintfNum(p_buf, p_last, ui64, zero, hex, width);
			++fmt;
		}
		else
		{
			// 没有格式控制符%时，直接将fmt当前指向的字符赋给buf当前指向的位置
			*p_buf++ = *fmt++;
		}
	}

	return p_buf;
}


/**
 * 功能：
	以一个指定的宽度width把一个数字ui64显示在p_buf对应的内存中,实际位数不参数宽度时，以zero填充

 * 输入参数：(u_char* p_buf, u_char* p_last, uint64_t ui64, u_char zero, uintptr_t hexadcimal, uintptr_t width)
	p_buf 往这里放按格式转换后的数据
	p_last 放的数据不要超过这里
	ui64 显示的数字
	zero 显示内容时，格式字符%后边接的是否是个'0',如果是zero = '0'，否则zero = ' '
		【一般显示的数字位数不足要求的，则用这个字符填充】，比如要显示10位，而实际只有7位，则后边填充3个这个字符
	hexadcimal 是否显示成十六进制数字 0不按16进制，1按16进制且小写显示，2按16进制且大写显示
	width 希望ui64显示的宽度值【如果实际显示的内容不够，则后头用zero填充】

 * 返回值：
	转换放好的数据后面一个指针位

 * 调用了函数：
	无

 * 其他说明：
	此函数只由本文件内的VslPrintf()调用

 * 例子说明：

 */
static u_char* SprintfNum(u_char* p_buf, u_char* p_last, uint64_t ui64, u_char zero, uintptr_t hexadcimal, uintptr_t width)
{
	uint32_t ui32 = 0;
	u_char arr_hex[] = "0123456789abcdef";
	u_char arr_HEX[] = "0123456789ABCDEF";

	// #define NGX_INT64_LEN   (sizeof("-9223372036854775808") - 1)     = 20   ，注意这里是sizeof是包括末尾的\0，不是strlen
	// arr_temp[21];
	u_char arr_tmp[INT64_LEN + 1];
	u_char* p_arrtmp = NULL;
	p_arrtmp = arr_tmp + INT64_LEN;  // INT64_LEN = 20,指向的是 arr_tmp[20]那个位置，也就是数组最后一个元素位置

	if (0 == hexadcimal)
	{
		if (ui64 <= (uint64_t)MAX_UINT32_VALUE)
		{
			ui32 = (uint32_t)ui64;
			do
			{
				--p_arrtmp;
				*p_arrtmp = (u_char)(ui32 % 10 + '0');

			} while (ui32 /= 10);
		}
		else
		{
			do
			{
				--p_arrtmp;
				*p_arrtmp = (u_char)(ui64 % 10 + '0');

			} while (ui64 /= 10);
		}
	}
	else if (1 == hexadcimal)
	{
		// 比如我显示一个1,234,567【十进制数】，他对应的十六进制数实际是 12 D687 ，那怎么显示出这个12D687来呢？
		// ui64 & 0xf，就等于把 一个数的最末尾的4个二进制位拿出来
		// ui64 & 0xf  其实就能分别得到 这个16进制数也就是 7,8,6,D,2,1这个数字，转成 (uint32_t) ，
		// 然后以这个为hex的下标，找到这几个数字的对应的能够显示的字符；
		do
		{
			--p_arrtmp;
			*p_arrtmp = arr_hex[(uint32_t)(ui64 & 0xf)];
		} while (ui64 >>= 4);
	}
	else
	{
		do
		{
			--p_arrtmp;
			*p_arrtmp = arr_HEX[(uint32_t)(ui64 & 0xf)];
		} while (ui64 >>= 4);
	}

	size_t len = (arr_tmp + INT64_LEN) - p_arrtmp;   // 得到这个数字的宽度，比如 “7654321”这个数字 ,len = 7
	size_t tmp = len;
	while ((tmp++ < width) && (p_buf < p_last))
	{
		*p_buf++ = zero;   // 宽度不足的用zero字符添加
	}

	// 发现如果往buf里拷贝“7654321”后，会导致buf不够长【剩余的空间不够拷贝整个数字】则计算还能存下多长数据
	if ((p_buf + len) >= p_last)
	{
		len = p_last - p_buf;
	}

	// 返回拷贝数据后面一位的指针位置

	return ngx_cpymem(p_buf, p_arrtmp, len);
}



