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
/* ## ------------------------------------------------------------------- ## */
/* ##                                                                     ## */
/* ##                                                                     ## */
/* ## ------------------------------------------------------------------- ## */
/* ##                                                                     ## */
/* ##  Changes ...: 2015-23-2015 (muhammad.ahmad@seagate.com)             ## */
/* ##               - Added new class to do ZBD related stuff             ## */
/* ##                                                                     ## */
/* ######################################################################### */

//#include "ZBDTarget.h"
#include "IOManager.h"


ZBDTarget::ZBDTarget()
{
	m_zone_index = 0;
	need_reset = 1;
	IoMgr = NULL;
	SetName("\0");
	m_max_open_zones = 128; //FIXME: Once the spec is out, this info 
	                              // needs to come from IDENTIFY DATA
	m_open_zone_list.clear();
}

ZBDTarget::ZBDTarget(const ZBDTarget &tgt )
{	
	need_reset = tgt.need_reset;
	SetName( tgt.name );
	zone_size_in_sectors = tgt.zone_size_in_sectors;
	number_of_zones = tgt.number_of_zones;
	max_lba_supported = tgt.max_lba_supported;
	m_zones = tgt.m_zones;
	m_zone_index = tgt.m_zone_index;	
	IoMgr = tgt.IoMgr;
	m_open_zone_list = tgt.m_open_zone_list;
	m_max_open_zones = tgt.m_max_open_zones; 
}

ZBDTarget::~ZBDTarget()
{
	m_mutex.lock();

	m_zones.clear();
	m_open_zone_list.clear();

	m_mutex.unlock();
	
}

void ZBDTarget::SetName(const char * n)
{
	memcpy(name, n,MAX_NAME);//sizeof?
}

void ZBDTarget::SetSizeInfo(DWORDLONG numberOfZones, DWORD zoneSizeInSectors, DWORDLONG maxLba)
{
	number_of_zones = numberOfZones;
	zone_size_in_sectors = zoneSizeInSectors; //0x80000; //524288 	
	max_lba_supported = maxLba;
}

void ZBDTarget::InitZBDData()
{
	unsigned int i = 0;
	ZBD_BAND_DESC desc; 
	//TODO: Get the C8 Read Log Ext. 
	//zone_size_in_sectors = 0x80000; //524288 
	//number_of_zones = 17707;
	//max_lba_supported = 9283397591;
	if(IoMgr != NULL)
	{
		if (IoMgr->max_open_zones > 0)
		{
			m_max_open_zones = IoMgr->max_open_zones;
		}
	}
	else
	{
		m_max_open_zones = 128; //FIXME: Once the spec is out, this info 
	                              // needs to come from IDENTIFY DATA
	}
	
	m_mutex.lock();
	m_zones.clear();
	m_zones.reserve(number_of_zones);

	//TODO: Get the C6 Read Log Ext. For now just hard code. 
	desc.band_size_in_sectors = zone_size_in_sectors;
	desc.zone_state = 0;
	desc.zone_type = 1;
	desc.pending = 0;
	desc.used = 0;
	for(i=0; i<number_of_zones; i++)
	{
		desc.optional_band_id = i;
		desc.band_start_lba = desc.band_write_ptr = (uint64_t)((uint64_t)i * (uint64_t)desc.band_size_in_sectors);		
		m_zones.push_back(desc);
	}
	need_reset = 0;
	m_open_zone_list.clear();
	m_mutex.unlock();
}

void ZBDTarget::ClearZBDData()
{
	m_mutex.lock();
	m_zones.clear();
	m_open_zone_list.clear();
	m_mutex.unlock();
}


void ZBDTarget::UpdateZoneInformation(DWORD idx, DWORD request_size)
{
	vector<DWORD>::iterator ittr; 
	uint64_t temp_wp = m_zones[idx].band_write_ptr + request_size/IDENTIFY_BUFFER_SIZE;
	need_reset = 1;
	if(request_size < 4096) // FIXME: This needs to be from drive IDENTIFY data
		temp_wp = m_zones[idx].band_write_ptr + 8; // WP always incriments 4k or physical Block of drive. 

	m_mutex.lock();
	//Most likely case
	if(temp_wp < (m_zones[idx].band_start_lba + m_zones[idx].band_size_in_sectors))
	{
		m_zones[idx].band_write_ptr = temp_wp;
		m_zones[idx].pending--;
	}
	else if(temp_wp > (m_zones[idx].band_start_lba + m_zones[idx].band_size_in_sectors))
	{
		//We went past the zone boundry. 
		m_zones[idx].band_write_ptr = m_zones[idx].band_start_lba + m_zones[idx].band_size_in_sectors;		
		m_zones[idx].pending = -1; //Requires reset
		//TODO: Investigate if this logic needs to be in Write pointer because 
		//      in an unlikely event, that pointer might be picked in the next random
		//      pick. 
		m_zones[idx+1].band_write_ptr = temp_wp - m_zones[idx].band_write_ptr;
	}
	else
	{
		m_zones[idx].band_write_ptr = temp_wp;
		m_zones[idx].pending = -1; //Requires reset
	}	

	//if zone needs a reset, remove it. 
	if(m_zones[idx].pending == -1)
	{
		ittr = find(m_open_zone_list.begin(), m_open_zone_list.end(), idx);
		if ( ittr != m_open_zone_list.end() )
		{
			//if(m_open_zone_list.size() > 1)
			{
				swap(*ittr,m_open_zone_list.back() );
			}
			m_open_zone_list.pop_back();
		}
		else
		{
#if defined (_GEN_LOG_FILE)
			if(IoMgr != NULL)
			{
				IoMgr->m_logFile <<  __FUNCTION__ << " Error updating zone " << idx << endl;
			}
#endif
		}
	}

	m_mutex.unlock();

#if defined (_GEN_LOG_FILE)
	if(IoMgr != NULL)
	{
		IoMgr->m_logFile <<  __FUNCTION__ << " : updated zone " << idx << " size " << dec << request_size << endl;
	}
#endif
	
}

int ZBDTarget::SetZBDSeqWp(DWORDLONG& offset, int& zone_index)
{
	int status = 1; 
	vector<DWORD>::iterator ittr; 
	zone_index = offset/(zone_size_in_sectors * IDENTIFY_BUFFER_SIZE); // FIXME: Should be logical block	

	do 
	{			
		ittr = find(m_open_zone_list.begin(), m_open_zone_list.end(), zone_index);		
		//If we already have this in our list.
		if ( ittr != m_open_zone_list.end() )
		{ 
			//And it is not one pending to be remove (unlikely)
			//We don't care if a zone has pending I/O cos this is sequential and higher layer will take care of queue depth. 
			if (m_zones[zone_index].pending >= 0)
			{
				m_mutex.lock();
				//Fix IDENTIFY_BUFFER_SIZE to be true logical sector size from drive. 			
				offset= m_zones[zone_index].band_write_ptr * IDENTIFY_BUFFER_SIZE;

				m_zone_index = zone_index;
				m_zones[zone_index].pending++;
				m_zones[zone_index].used++;
				status = 0;
				m_mutex.unlock();
			}
			else
			{
				//in the unlikely case, let try the next zone
				zone_index++;
			}
		}
		else if (m_open_zone_list.size() < m_max_open_zones)
		{
			m_mutex.lock();
			//Fix IDENTIFY_BUFFER_SIZE to be true logical sector size from drive. 			
			offset= m_zones[zone_index].band_write_ptr * IDENTIFY_BUFFER_SIZE;
			m_open_zone_list.push_back(zone_index);
			m_zone_index = zone_index;
			m_zones[zone_index].pending++;
			m_zones[zone_index].used++;
			status = 0;
			m_mutex.unlock();
		}
		else
		{
			//In a mixed read/write seq/rand waiting on the offset can cause a live deadlock. 
			//Because the offset gets added to with reads and in some cases the 
			//next sequential write to be in a zone may not be at an offset with an open zone. 
			//So we just pick the last zone used. 
			m_mutex.lock();
			zone_index = m_zone_index;
			//zone_index = ::rand() % m_max_open_zones;
			//Fix Me: IDENTIFY_BUFFER_SIZE to be true logical sector size from drive. 			
			offset= m_zones[zone_index].band_write_ptr * IDENTIFY_BUFFER_SIZE;						
			m_zones[zone_index].pending++;
			m_zones[zone_index].used++;
			status = 0;
			m_mutex.unlock();
#if defined (_GEN_LOG_FILE)
			if(IoMgr != NULL)
			{
				IoMgr->m_logFile << __FUNCTION__ << "  , offset , " << hex << "0x" << offset << " zone index " << zone_index << endl;
			}
#endif			
		}

	} while(status);
	
#if defined (_GEN_LOG_FILE)
	if(IoMgr != NULL)
	{
		IoMgr->m_logFile << __FUNCTION__ << "  , offset , " << hex << "0x" << offset << " , LBA , 0x" << m_zones[m_zone_index].band_write_ptr << " , index , " << dec << zone_index << " , used , " << m_zones[m_zone_index].used <<  endl;
	}
#endif	

	return status; 
}

int ZBDTarget::GetZBDRandWp(DWORDLONG& offset, int& zone_index/*,BOOL rand*/)
{	
	BOOL found_zone = false;	
	DWORD open_zone_idx = 0; 
	m_mutex.lock();
	do
	{
		if(m_open_zone_list.size() < m_max_open_zones)
		{

			m_zone_index = std::rand() % m_zones.size();
			//If the zone is already in the open zone list, don't add it again.
			// Just go through the loop again
			if ( find(m_open_zone_list.begin(), m_open_zone_list.end(), m_zone_index)\
								==m_open_zone_list.end())
			{
				if( m_zones[m_zone_index].pending == 0 )
				{
					//Fix IDENTIFY_BUFFER_SIZE to be true logical sector size from drive. 			
					offset= m_zones[m_zone_index].band_write_ptr * IDENTIFY_BUFFER_SIZE;
					m_zones[m_zone_index].pending++;
					m_zones[m_zone_index].used++;
					found_zone = true;			
					m_open_zone_list.push_back(m_zone_index);
				}
			}
		}
		else //this means we have max zones open. 
		{
			open_zone_idx = std::rand() % m_open_zone_list.size();
			m_zone_index = m_open_zone_list[open_zone_idx];
			if( m_zones[m_zone_index].pending == 0 )
			{
				//Fix IDENTIFY_BUFFER_SIZE to be true logical sector size from drive. 			
				offset= m_zones[m_zone_index].band_write_ptr * IDENTIFY_BUFFER_SIZE;
				m_zones[m_zone_index].pending++;
				m_zones[m_zone_index].used++;
				found_zone = true;			
			}
			else
			{
#if defined (_GEN_LOG_FILE)
				if(IoMgr != NULL)
				{
					IoMgr->m_logFile << __FUNCTION__ << " : Skipping zone " << m_zone_index << " from m_zones as pending="<< m_zones[m_zone_index].pending << endl;
				}
#endif
				continue; //skip, there is an outstanding command or needs reset
			}			
		}
	} while (!found_zone);
	zone_index = m_zone_index;
	m_mutex.unlock();
#if defined (_GEN_LOG_FILE)
	if(IoMgr != NULL)
	{
		IoMgr->m_logFile << __FUNCTION__ << "  , offset , " << hex << "0x" << offset << " , LBA , 0x" << m_zones[m_zone_index].band_write_ptr << " , index , " << dec << zone_index << " , used , " << m_zones[m_zone_index].used <<  endl;
	}
#endif

	return 0; //TODO: Use it later to return unlikely error. 
}


