// crc32算法头文件
#ifndef __NGX_C_CRC32_H__
#define __NGX_C_CRC32_H__

#include <stddef.h>  // NULL

class CCRC32
{
private:
	CCRC32();
    CCRC32(const CCRC32&) {};
    CCRC32& operator =(const CCRC32& obj) {return const_cast<CCRC32&>(obj);};

	static CCRC32* mp_instance;

public:
	//unsigned long crc32_table[256];  // Lookup table arrays
	unsigned int crc32_table[256];     // Lookup table arrays

public:	
    ~CCRC32();

	static CCRC32* GetInstance() 
	{
		if(mp_instance == NULL)
		{
			// 锁
			if(mp_instance == NULL)
			{				
				mp_instance = new CCRC32();
				static CGarhuishou cl; 
			}
			// 放锁
		}
        
		return mp_instance;
	}	
	class CGarhuishou 
	{
	public:				
		~CGarhuishou()
		{
			if (CCRC32::mp_instance)
			{						
				delete CCRC32::mp_instance;
				CCRC32::mp_instance = NULL;				
			}
		}
	};
	
	void InitCRC32Table();
    unsigned int Reflect(unsigned int ref, char ch);     // Reflects CRC bits in the lookup table
    int GetCRC(unsigned char* buffer, unsigned int dw_size);
    
};

#endif





