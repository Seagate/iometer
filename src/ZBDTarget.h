/* ######################################################################### */
/* ##                                                                     ## */
/* ##  (Dynamo) / ZBDTarget.cpp		                                      ## */
/* ##                                                                     ## */
/* ## ------------------------------------------------------------------- ## */
/* ##                                                                     ## */
/* ##  Job .......: Contains the class the ZBD Target					  ## */
/* ##                                                                     ## */
/* ## ------------------------------------------------------------------- ## */
/* ##                                                                     ## */
/* ##                                                                     ## */
/* ##                                                                     ## */
/* ## ------------------------------------------------------------------- ## */
/* ##                                                                     ## */
/* ##  Changes ...: 2015-23-2015 (muhammad.ahmad@seagate.com)             ## */
/* ##               - Added new class to do ZBD related stuff             ## */
/* ##                                                                     ## */
/* ######################################################################### */

#ifndef ZBD_TARGET_DEFINED
#define ZBD_TARGET_DEFINED

#include <mutex>
#include <vector>
#include "IOCommon.h"

#if defined(IOMTR_OSFAMILY_WINDOWS) || defined(IOMTR_OSFAMILY_NETWARE)
namespace std {
}
#endif
using namespace std;

#define MAX_ZBD_TARGETS 2

class Manager;

typedef struct _ZBD_BAND_DESC {
	uint8_t		zone_type;
	uint8_t		zone_state;
	uint16_t	reserved; 
	uint32_t	band_size_in_sectors;
	uint64_t	band_start_lba;
	uint64_t	band_write_ptr;
	uint64_t	optional_band_id;
	int32_t		pending;
	DWORD		last_request_size;
	uint32_t	used;
} ZBD_BAND_DESC;
typedef vector<ZBD_BAND_DESC> ZBD_BANDS;


class ZBDTarget {
	public:
		ZBDTarget();
		ZBDTarget(const ZBDTarget &tgt );
		~ZBDTarget();	
		char * GetName() { return name; }
		void SetName(const char * n);
		void SetSizeInfo(DWORDLONG numberOfZones, DWORD zoneSizeInSectors , DWORDLONG maxLba);
		void InitZBDData();
		void ClearZBDData();		
		void UpdateZoneInformation(DWORD idx, DWORD request_size);
		int SetZBDSeqWp(DWORDLONG& offset, int& zone_index,  DWORD request_size);
		int GetZBDRandWp(DWORDLONG& offset, int& zone_index);
		//void AcquireLock() { m_mutex.lock(); }
		//void ReleaseLock() { m_mutex.unlock(); }
		void SetIOManager(Manager * mgr) { this->IoMgr = mgr; } 
		Manager * GetIOManager() { return IoMgr; }
		BOOL need_reset;
	protected:
		char name[MAX_NAME]; // Device name -- for Windows, this is used to display the friendly name
					// that has no resembelance to the string representing the device.		
		DWORD zone_size_in_sectors;
		DWORD number_of_zones;
		DWORD m_max_open_zones;
		DWORDLONG max_lba_supported; 	
		ZBD_BANDS m_zones;
		DWORD m_zone_index;		
		Manager * IoMgr;	
		mutex m_mutex;
		vector<DWORD> m_open_zone_list;		
};

#endif
