#include "HwBufferMgr.h"

#include <sys/sysinfo.h>
#include <limits.h>

using namespace lima;

/*******************************************************************
 * \brief BufferAllocMgr destructor
 *******************************************************************/

BufferAllocMgr::~BufferAllocMgr()
{
}

void BufferAllocMgr::clearBuffer(int buffer_nb)
{
	void *ptr = getBufferPtr(buffer_nb);
	const FrameDim& frame_dim = getFrameDim();
	memset(ptr, 0, frame_dim.getMemSize());
}

void BufferAllocMgr::clearAllBuffers()
{
	for (int i = 0; i < getNbBuffers(); i++)
		clearBuffer(i);
}


/*******************************************************************
 * \brief SoftBufferAllocMgr constructor
 *******************************************************************/

SoftBufferAllocMgr::SoftBufferAllocMgr()
{
}

SoftBufferAllocMgr::~SoftBufferAllocMgr()
{
	releaseBuffers();
}

int SoftBufferAllocMgr::getSystemMem(int& mem_unit)
{
	if (mem_unit < 0)
		throw LIMA_HW_EXC(InvalidValue, "Invalid mem_unit value");

        struct sysinfo s_info;
	if (sysinfo(&s_info) < 0)
		throw LIMA_HW_EXC(Error, "Error calling sysinfo");

        long long tot_mem = s_info.totalram;
	tot_mem *= s_info.mem_unit;

	if (mem_unit == 0) 
		mem_unit = s_info.mem_unit;

	long long mem_blocks = tot_mem / mem_unit;
	if (mem_blocks > INT_MAX)
		throw LIMA_HW_EXC(Error, "Too much memory to be described "
				  "with the given mem_unit");
	return mem_blocks;
}

int SoftBufferAllocMgr::getMaxNbBuffers(const FrameDim& frame_dim)
{
	int frame_size = frame_dim.getMemSize();
	if (frame_size <= 0)
		throw LIMA_HW_EXC(InvalidValue, "Invalid FrameDim");

	long long tot_buffers = getSystemMem(frame_size);
	return tot_buffers * 3 / 4;
}

void SoftBufferAllocMgr::allocBuffers(int nb_buffers, 
				 const FrameDim& frame_dim)
{
	int frame_size = frame_dim.getMemSize();
	if (frame_size <= 0)
		throw LIMA_HW_EXC(InvalidValue, "Invalid FrameDim");

	if ((frame_dim == m_frame_dim) && (nb_buffers == getNbBuffers()))
		return;

	releaseBuffers();

	int max_buffers = getMaxNbBuffers(frame_dim);
	if ((nb_buffers < 1) || (nb_buffers > max_buffers))
		throw LIMA_HW_EXC(InvalidValue, "Invalid number of buffers");

	try {
		m_buffer_list.reserve(nb_buffers);
		for (int i = 0; i < nb_buffers; ++i) 
			m_buffer_list.push_back(new char[frame_size]);
	} catch (...) {
		releaseBuffers();
		throw;
	}

	m_frame_dim = frame_dim;
}

void SoftBufferAllocMgr::releaseBuffers()
{
	BufferList& bl = m_buffer_list;
	for (BufferListCIt it = bl.begin(); it != bl.end(); ++it)
		delete [] *it;
	bl.clear();
	m_frame_dim = FrameDim();
}

const FrameDim& SoftBufferAllocMgr::getFrameDim()
{
	return m_frame_dim;
}

int SoftBufferAllocMgr::getNbBuffers()
{
	return m_buffer_list.size();
}

void *SoftBufferAllocMgr::getBufferPtr(int buffer_nb)
{
	return m_buffer_list[buffer_nb];
}


/*******************************************************************
 * \brief StdBufferCbMgr constructor
 *******************************************************************/

StdBufferCbMgr::StdBufferCbMgr(BufferAllocMgr *alloc_mgr)
{
	m_fcb_act = false;
	m_alloc_mgr = alloc_mgr;
	m_int_alloc_mgr = !alloc_mgr;
	if (m_int_alloc_mgr)
		m_alloc_mgr = new SoftBufferAllocMgr();
}

StdBufferCbMgr::~StdBufferCbMgr()
{
	if (m_int_alloc_mgr)
		delete m_alloc_mgr;
}

int StdBufferCbMgr::getMaxNbBuffers(const FrameDim& frame_dim)
{
	return m_alloc_mgr->getMaxNbBuffers(frame_dim);
}

void StdBufferCbMgr::allocBuffers(int nb_buffers, 
				  const FrameDim& frame_dim)
{
	int frame_size = frame_dim.getMemSize();
	if (frame_size <= 0)
		throw LIMA_HW_EXC(InvalidValue, "Invalid FrameDim");

	if ((frame_dim == getFrameDim()) && (nb_buffers == getNbBuffers()))
		return;

	releaseBuffers();

	try {
		m_alloc_mgr->allocBuffers(nb_buffers, frame_dim);

		m_ts_list.reserve(nb_buffers);
		for (int i = 0; i < nb_buffers; ++i)
			m_ts_list.push_back(Timestamp());
	} catch (...) {
		releaseBuffers();
		throw;
	}
}

void StdBufferCbMgr::releaseBuffers()
{
	m_alloc_mgr->releaseBuffers();
	m_ts_list.clear();
}

void StdBufferCbMgr::setStartTimestamp(Timestamp start_ts)
{
	if (!start_ts.isSet())
		throw LIMA_HW_EXC(InvalidValue, "Invalid start timestamp");
	m_start_ts = start_ts;
}

void StdBufferCbMgr::setFrameCallbackActive(bool cb_active)
{
	m_fcb_act = cb_active;
}

bool StdBufferCbMgr::newFrameReady(int acq_frame_nb)
{
	if (!m_fcb_act)
		return false;

        int buffer_nb = acq_frame_nb % getNbBuffers();
	FrameInfoType frame_info(acq_frame_nb, getBufferPtr(buffer_nb), 
				 &getFrameDim(), Timestamp::now());
	return HwFrameCallbackGen::newFrameReady(frame_info);
}

const FrameDim& StdBufferCbMgr::getFrameDim() const
{
	return m_alloc_mgr->getFrameDim();
}

int StdBufferCbMgr::getNbBuffers() const
{
	return m_alloc_mgr->getNbBuffers();
}

void *StdBufferCbMgr::getBufferPtr(int buffer_nb) const
{
	return m_alloc_mgr->getBufferPtr(buffer_nb);
}

Timestamp StdBufferCbMgr::getBufferTimestamp(int buffer_nb) const
{
	const Timestamp& ts = m_ts_list[buffer_nb];
	if (!ts.isSet())
		return ts;
	return ts - m_start_ts;
}

